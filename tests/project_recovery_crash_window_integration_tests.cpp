#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_job_repository.hpp"
#include "biocore/application/i_output_artifact_inspector.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/output_artifact_service.hpp"
#include "biocore/application/project_recovery_service.hpp"
#include "biocore/application/worker_event_ingestion_error.hpp"
#include "biocore/application/worker_event_ingestion_session.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_job_repository.hpp"
#include "biocore/infrastructure/sqlite/sqlite_managed_file_repository.hpp"

namespace {
using namespace biocore;

class FailingJobRepo final : public application::IJobRepository {
public:
    explicit FailingJobRepo(infrastructure::sqlite::SqliteJobRepository& delegate) : delegate_{delegate} {}

    bool add(const domain::Job& job) override { return delegate_.add(job); }
    std::optional<domain::Job> find_by_id(std::string_view id) override {
        return delegate_.find_by_id(id);
    }
    std::vector<domain::Job> list() override { return delegate_.list(); }
    bool update_runtime_state(const domain::Job& job, const std::int64_t expected) override {
        ++update_calls;
        if (update_calls == fail_on_update_call) return false;
        return delegate_.update_runtime_state(job, expected);
    }

    int update_calls{0};
    int fail_on_update_call{2};

private:
    infrastructure::sqlite::SqliteJobRepository& delegate_;
};

class IDs final : public application::IIdGenerator {
public:
    std::string generate() override { return "artifact-1"; }
};

class Clock final : public application::IUtcClock {
public:
    std::string now_utc_iso8601() override { return "2026-08-07T12:00:00Z"; }
};

class Inspector final : public application::IOutputArtifactInspector {
public:
    application::InspectedOutputArtifact inspect_existing_output(
        const std::string_view relative_project_path
    ) override {
        return {
            .display_name = "result.out",
            .managed_path = "/project/outputs/job-1--copy--result.out",
            .relative_project_path = std::string{relative_project_path},
            .size_bytes = 7,
            .modified_at_utc = std::nullopt,
            .checksum_algorithm = "sha256",
            .checksum_value = std::string(64U, 'a'),
        };
    }
};

[[nodiscard]] domain::Job preparing_job() {
    return domain::Job{
        "job-1", std::nullopt, std::string{"pipeline"}, std::string{"1"},
        domain::JobStatus::preparing, domain::JobPriority::normal, 0.0, std::nullopt,
        "2026-08-07T11:00:00Z", "2026-08-07T11:05:00Z",
        std::string{"2026-08-07T11:05:00Z"}, std::nullopt, 3,
    };
}

[[nodiscard]] application::WorkerLifecycleEvent event(
    const application::WorkerLifecycleEventType type, const std::uint64_t sequence
) {
    application::WorkerLifecycleEvent value;
    value.type = type;
    value.job_id = "job-1";
    value.launch_revision = 3;
    value.sequence = sequence;
    value.worker_timestamp_utc = "2026-08-07T11:10:00Z";
    return value;
}

[[nodiscard]] bool crash_window_reconciles() {
    infrastructure::sqlite::SqliteConnection connection{std::filesystem::path{":memory:"}};
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    infrastructure::sqlite::SqliteJobRepository sqlite_jobs{connection};
    infrastructure::sqlite::SqliteManagedFileRepository files{connection};
    if (!sqlite_jobs.add(preparing_job())) return false;

    FailingJobRepo failing_jobs{sqlite_jobs};
    IDs ids;
    Clock clock;
    Inspector inspector;
    application::JobService live_jobs{failing_jobs, ids, clock};
    application::OutputArtifactService artifact_service{files, inspector, ids, clock};
    application::WorkerEventIngestionSession session{
        live_jobs, "job-1", 3, &artifact_service, nullptr
    };

    static_cast<void>(session.ingest(event(application::WorkerLifecycleEventType::ready, 1U)));
    auto artifact = event(application::WorkerLifecycleEventType::artifact, 2U);
    artifact.artifact_step_id = "copy";
    artifact.artifact_output_port = "result";
    artifact.artifact_plugin_id = "org.biocore.demo";
    artifact.artifact_plugin_version = "0.1.0";
    artifact.artifact_module_id = "org.biocore.demo.copy";
    artifact.artifact_file_type = "txt";
    artifact.artifact_relative_project_path = "outputs/job-1--copy--result.out";
    static_cast<void>(session.ingest(artifact));

    auto progress = event(application::WorkerLifecycleEventType::progress, 3U);
    progress.progress = 0.6;
    progress.active_step_id = "copy";
    try {
        static_cast<void>(session.ingest(progress));
        return false;
    } catch (const application::WorkerEventIngestionError& error) {
        if (error.code() != application::WorkerEventIngestionErrorCode::concurrent_update) {
            return false;
        }
    }

    const auto stale = sqlite_jobs.find_by_id("job-1");
    const auto checkpoint = files.latest_generated_output_progress("job-1");
    if (!stale.has_value() || stale->status() != domain::JobStatus::running ||
        stale->progress() != 0.0 || checkpoint != std::optional<double>{0.6}) {
        return false;
    }

    application::JobService recovered_jobs{sqlite_jobs, ids, clock};
    application::ProjectRecoveryService recovery{
        recovered_jobs, files, nullptr, nullptr,
        application::ProjectRecoveryPolicy{
            .cleanup_interrupted_jobs = false,
            .purge_expired_quarantine = false,
            .quarantine_retention = std::chrono::hours{24 * 30},
        }
    };
    const auto recovered = recovery.recover();
    const auto final_job = sqlite_jobs.find_by_id("job-1");
    return recovered.recovered_jobs.size() == 1U && recovered.issues.empty() &&
           final_job.has_value() && final_job->status() == domain::JobStatus::interrupted &&
           final_job->progress() == 0.6 && final_job->revision() == 5;
}

}  // namespace

int main() {
    if (!crash_window_reconciles()) {
        std::cerr << "Project recovery crash-window integration failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
