#include <sqlite3.h>

#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/domain/job_failure.hpp"
#include "biocore/domain/job_status.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"
#include "biocore/infrastructure/sqlite/sqlite_job_repository.hpp"

namespace {

using namespace biocore;

class FixedIdGenerator final : public application::IIdGenerator {
public:
    std::string generate() override { return "failure-job"; }
};

class FakeClock final : public application::IUtcClock {
public:
    explicit FakeClock(std::deque<std::string> values) : values_{std::move(values)} {}
    std::string now_utc_iso8601() override {
        if (values_.empty()) throw std::runtime_error("clock exhausted");
        auto value = std::move(values_.front());
        values_.pop_front();
        return value;
    }
private:
    std::deque<std::string> values_;
};

[[nodiscard]] bool statement_throws(
    infrastructure::sqlite::SqliteConnection& connection,
    const std::string& sql
) {
    try {
        connection.execute(sql);
    } catch (const infrastructure::sqlite::SqliteError&) {
        return true;
    }
    return false;
}

[[nodiscard]] bool worker_failure_round_trip_and_retry_clear() {
    infrastructure::sqlite::SqliteConnection connection{std::filesystem::path{":memory:"}};
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    infrastructure::sqlite::SqliteJobRepository repository{connection};
    FixedIdGenerator ids;
    FakeClock clock{{"create", "queue", "prepare", "fail", "retry"}};
    application::JobService jobs{repository, ids, clock};

    auto job = jobs.create(application::CreateJobRequest{
        .analysis_id = std::string{"failure-analysis"},
        .pipeline_id = std::string{"org.biocore.test"},
        .pipeline_version = std::string{"0.1.0"},
        .priority = domain::JobPriority::normal,
    });
    job = jobs.transition(job.id(), domain::JobStatus::queued, 0.0, std::nullopt);
    job = jobs.transition(job.id(), domain::JobStatus::preparing, 0.1, std::string{"launch"});
    job = jobs.transition(
        job.id(),
        domain::JobStatus::failed,
        0.1,
        std::nullopt,
        application::JobFailureContext{
            .kind = domain::JobFailureKind::worker_reported_failure,
            .message = "plugin rejected malformed FASTQ record",
            .exit_code = 17,
            .worker_timestamp_utc = std::string{"worker-time"},
        }
    );

    const auto stored = repository.find_by_id(job.id());
    if (!stored.has_value() || stored->status() != domain::JobStatus::failed ||
        !stored->failure().has_value()) {
        return false;
    }
    const auto& failure = *stored->failure();
    if (failure.kind() != domain::JobFailureKind::worker_reported_failure ||
        failure.message() != "plugin rejected malformed FASTQ record" ||
        failure.exit_code() != std::optional<std::int64_t>{17} ||
        failure.worker_timestamp_utc() != std::optional<std::string>{"worker-time"} ||
        failure.recorded_at_utc() != "fail") {
        return false;
    }

    // Failed is intentionally not retryable. Exercise clearing on an interrupted job below.
    return true;
}

[[nodiscard]] bool interrupted_failure_clears_when_requeued() {
    infrastructure::sqlite::SqliteConnection connection{std::filesystem::path{":memory:"}};
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    infrastructure::sqlite::SqliteJobRepository repository{connection};
    FixedIdGenerator ids;
    FakeClock clock{{"create", "queue", "prepare", "interrupt", "retry"}};
    application::JobService jobs{repository, ids, clock};

    auto job = jobs.create(application::CreateJobRequest{});
    job = jobs.transition(job.id(), domain::JobStatus::queued, 0.0, std::nullopt);
    job = jobs.transition(job.id(), domain::JobStatus::preparing, 0.2, std::string{"launch"});
    job = jobs.transition(
        job.id(),
        domain::JobStatus::interrupted,
        0.2,
        std::nullopt,
        application::JobFailureContext{
            .kind = domain::JobFailureKind::process_exit_without_terminal,
            .message = "worker exited before terminal event",
            .exit_code = 9,
            .worker_timestamp_utc = std::nullopt,
        }
    );
    if (!job.failure().has_value()) return false;

    job = jobs.transition(job.id(), domain::JobStatus::queued, 0.2, std::nullopt);
    const auto stored = repository.find_by_id(job.id());
    return stored.has_value() && stored->status() == domain::JobStatus::queued &&
           !stored->failure().has_value();
}

[[nodiscard]] bool version_six_backfills_legacy_terminal_evidence() {
    infrastructure::sqlite::SqliteConnection connection{std::filesystem::path{":memory:"}};
    connection.execute(R"sql(
        CREATE TABLE schema_migrations(
            version INTEGER PRIMARY KEY NOT NULL,
            name TEXT NOT NULL,
            applied_at_utc TEXT NOT NULL
        );
        INSERT INTO schema_migrations(version, name, applied_at_utc) VALUES
            (1, 'create_project_core_tables', 't'),
            (2, 'extend_jobs_for_repository', 't'),
            (3, 'register_generated_output_artifacts', 't'),
            (4, 'checkpoint_generated_output_progress', 't'),
            (5, 'require_generated_output_sha256', 't'),
            (6, 'associate_prepared_job_execution_plans', 't');
        CREATE TABLE jobs(
            id TEXT PRIMARY KEY NOT NULL,
            status TEXT NOT NULL,
            priority TEXT NOT NULL DEFAULT 'normal',
            progress REAL NOT NULL DEFAULT 0.0,
            active_step_id TEXT,
            created_at_utc TEXT NOT NULL,
            updated_at_utc TEXT NOT NULL,
            analysis_id TEXT,
            pipeline_id TEXT,
            pipeline_version TEXT,
            started_at_utc TEXT,
            finished_at_utc TEXT,
            revision INTEGER NOT NULL DEFAULT 0
        );
        INSERT INTO jobs(
            id, status, progress, created_at_utc, updated_at_utc, started_at_utc,
            finished_at_utc, revision
        ) VALUES
            ('legacy-failed', 'failed', 0.4, 'c', 'u', 's', 'f', 4),
            ('legacy-interrupted', 'interrupted', 0.7, 'c2', 'u2', 's2', NULL, 7);
    )sql");

    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    if (migrations.current_version() != 7) return false;
    infrastructure::sqlite::SqliteJobRepository repository{connection};
    const auto failed = repository.find_by_id("legacy-failed");
    const auto interrupted = repository.find_by_id("legacy-interrupted");
    return failed.has_value() && interrupted.has_value() &&
           failed->failure().has_value() && interrupted->failure().has_value() &&
           failed->failure()->kind() == domain::JobFailureKind::legacy_terminal_state &&
           interrupted->failure()->kind() == domain::JobFailureKind::legacy_terminal_state &&
           failed->failure()->recorded_at_utc() == "f" &&
           interrupted->failure()->recorded_at_utc() == "u2";
}

[[nodiscard]] bool schema_triggers_reject_inconsistent_evidence() {
    infrastructure::sqlite::SqliteConnection connection{std::filesystem::path{":memory:"}};
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();

    const bool missing_failure = statement_throws(connection, R"sql(
        INSERT INTO jobs(
            id, status, priority, progress, created_at_utc, updated_at_utc,
            started_at_utc, finished_at_utc, revision
        ) VALUES ('bad-failed', 'failed', 'normal', 0.0, 'c', 'u', 's', 'f', 0);
    )sql");
    const bool nonterminal_failure = statement_throws(connection, R"sql(
        INSERT INTO jobs(
            id, status, priority, progress, created_at_utc, updated_at_utc, revision,
            failure_kind, failure_message, failure_recorded_at_utc
        ) VALUES (
            'bad-draft', 'draft', 'normal', 0.0, 'c', 'u', 0,
            'legacy_terminal_state', 'not allowed', 'u'
        );
    )sql");
    const bool wrong_kind = statement_throws(connection, R"sql(
        INSERT INTO jobs(
            id, status, priority, progress, created_at_utc, updated_at_utc,
            started_at_utc, finished_at_utc, revision, failure_kind, failure_message,
            failure_exit_code, failure_worker_timestamp_utc, failure_recorded_at_utc
        ) VALUES (
            'bad-worker', 'interrupted', 'normal', 0.0, 'c', 'u', 's', 'f', 0,
            'worker_reported_failure', 'wrong terminal status', 2, 'worker-t', 'u'
        );
    )sql");
    return missing_failure && nonterminal_failure && wrong_kind;
}

}  // namespace

int main() {
    if (!worker_failure_round_trip_and_retry_clear()) {
        std::cerr << "Structured worker failure persistence contract failed\n";
        return EXIT_FAILURE;
    }
    if (!interrupted_failure_clears_when_requeued()) {
        std::cerr << "Interrupted failure retry-clear contract failed\n";
        return EXIT_FAILURE;
    }
    if (!version_six_backfills_legacy_terminal_evidence()) {
        std::cerr << "Schema-v6 terminal failure backfill contract failed\n";
        return EXIT_FAILURE;
    }
    if (!schema_triggers_reject_inconsistent_evidence()) {
        std::cerr << "Structured failure schema-trigger contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
