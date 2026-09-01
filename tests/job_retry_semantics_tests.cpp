#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_retry_service.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/job_service_error.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/job_failure.hpp"
#include "biocore/domain/job_status.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"
#include "biocore/infrastructure/sqlite/sqlite_job_repository.hpp"
#include "biocore/infrastructure/sqlite/sqlite_prepared_job_store.hpp"

namespace {

using namespace biocore;

class UnusedIdGenerator final : public application::IIdGenerator {
public:
    std::string generate() override { throw std::runtime_error("unused"); }
};

class SequenceClock final : public application::IUtcClock {
public:
    explicit SequenceClock(std::deque<std::string> values) : values_{std::move(values)} {}
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

[[nodiscard]] bool retry_contract() {
    infrastructure::sqlite::SqliteConnection connection{std::filesystem::path{":memory:"}};
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    if (migrations.current_version() != 8) return false;

    infrastructure::sqlite::SqliteJobRepository repository{connection};
    infrastructure::sqlite::SqlitePreparedJobStore prepared{connection};

    domain::Job queued{
        "retry-job",
        std::string{"analysis-retry"},
        std::string{"org.biocore.retry"},
        std::string{"1.0.0"},
        domain::JobStatus::queued,
        domain::JobPriority::normal,
        0.0,
        std::nullopt,
        "created",
        "queued",
        std::nullopt,
        std::nullopt,
        1,
    };
    const application::PreparedJobExecution initial_execution{
        .job_id = "retry-job",
        .attempt_number = 1,
        .launch_revision = 2,
        .pipeline_id = "org.biocore.retry",
        .pipeline_version = "1.0.0",
        .execution_plan_path = "plans/retry-job.json",
        .prepared_at_utc = "queued",
    };
    if (!prepared.add_prepared_job(queued, initial_execution)) return false;

    UnusedIdGenerator ids;
    SequenceClock clock{{
        "prepare-1", "run-1", "interrupt-1", "retry-1",
        "prepare-2", "run-2", "interrupt-2", "retry-2"
    }};
    application::JobService jobs{repository, ids, clock};
    application::JobRetryService retries{jobs, prepared, clock};

    auto job = jobs.transition("retry-job", domain::JobStatus::preparing, 0.0, std::nullopt);
    job = jobs.transition("retry-job", domain::JobStatus::running, 0.1, std::string{"step-a"});
    job = jobs.transition(
        "retry-job",
        domain::JobStatus::interrupted,
        0.3,
        std::nullopt,
        application::JobFailureContext{
            .kind = domain::JobFailureKind::process_exit_without_terminal,
            .message = "worker exited before terminal event",
            .exit_code = 9,
            .worker_timestamp_utc = std::nullopt,
        }
    );
    if (job.revision() != 4 || job.attempt_number() != 1 || !job.failure().has_value()) return false;

    try {
        static_cast<void>(jobs.transition(
            "retry-job", domain::JobStatus::queued, 0.0, std::nullopt
        ));
        return false;
    } catch (const std::invalid_argument&) {
    }

    const auto first_retry = retries.retry("retry-job");
    if (first_retry.job.status() != domain::JobStatus::queued ||
        first_retry.job.progress() != 0.0 || first_retry.job.failure().has_value() ||
        first_retry.job.started_at_utc().has_value() || first_retry.job.finished_at_utc().has_value() ||
        first_retry.job.attempt_number() != 2 || first_retry.job.revision() != 5 ||
        first_retry.execution.attempt_number != 2 || first_retry.execution.launch_revision != 6 ||
        first_retry.execution.execution_plan_path != "plans/retry-job.json" ||
        first_retry.execution.prepared_at_utc != "queued") {
        return false;
    }

    const auto stored_execution = prepared.find_execution("retry-job");
    if (!stored_execution.has_value() || stored_execution->attempt_number != 2 ||
        stored_execution->launch_revision != 6 ||
        stored_execution->execution_plan_path != "plans/retry-job.json") {
        return false;
    }

    if (!statement_throws(
            connection,
            "UPDATE job_execution_plans SET execution_plan_path = 'plans/mutated.json' "
            "WHERE job_id = 'retry-job';"
        ) ||
        !statement_throws(
            connection,
            "UPDATE job_execution_plans SET launch_revision = 5 WHERE job_id = 'retry-job';"
        )) {
        return false;
    }

    job = jobs.transition("retry-job", domain::JobStatus::preparing, 0.0, std::nullopt);
    if (job.revision() != 6 || job.attempt_number() != 2) return false;
    job = jobs.transition("retry-job", domain::JobStatus::running, 0.2, std::string{"step-a"});
    job = jobs.transition(
        "retry-job",
        domain::JobStatus::interrupted,
        0.5,
        std::nullopt,
        application::JobFailureContext{
            .kind = domain::JobFailureKind::heartbeat_timeout,
            .message = "heartbeat timeout",
            .exit_code = std::nullopt,
            .worker_timestamp_utc = std::nullopt,
        }
    );
    const auto second_retry = retries.retry("retry-job");
    if (second_retry.job.attempt_number() != 3 || second_retry.job.revision() != 9 ||
        second_retry.execution.attempt_number != 3 || second_retry.execution.launch_revision != 10 ||
        second_retry.execution.execution_plan_path != "plans/retry-job.json") {
        return false;
    }

    try {
        static_cast<void>(retries.retry("retry-job"));
        return false;
    } catch (const application::JobServiceError& error) {
        return error.code() == application::JobServiceErrorCode::job_not_retryable;
    }
}

}  // namespace

int main() {
    if (!retry_contract()) {
        std::cerr << "Job retry/recovery semantics contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
