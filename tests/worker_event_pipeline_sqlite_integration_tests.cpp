#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/worker_event_ingestion_session.hpp"
#include "biocore/application/worker_launch_request.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/job_status.hpp"
#include "biocore/infrastructure/platform_worker_supervisor.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_job_repository.hpp"

namespace {

namespace fs = std::filesystem;
using namespace biocore::application;
using biocore::domain::JobPriority;
using biocore::domain::JobStatus;
using biocore::infrastructure::PlatformWorkerSupervisor;
using biocore::infrastructure::WorkerProcessExit;
using biocore::infrastructure::sqlite::ProjectMigrationRunner;
using biocore::infrastructure::sqlite::SqliteConnection;
using biocore::infrastructure::sqlite::SqliteJobRepository;

[[nodiscard]] fs::path path_from_utf8(const std::string_view value) {
    const std::u8string utf8{
        reinterpret_cast<const char8_t*>(value.data()),
        reinterpret_cast<const char8_t*>(value.data() + value.size())
    };
    return fs::path{utf8};
}

class TemporaryProject final {
public:
    TemporaryProject() {
        const auto unique = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()
        );
        root_ = fs::temp_directory_path() / ("biocore-worker-events-" + unique);
        fs::create_directories(root_);
        root_ = fs::canonical(root_);
    }
    ~TemporaryProject() {
        std::error_code error;
        fs::remove_all(root_, error);
    }
    [[nodiscard]] const fs::path& root() const noexcept { return root_; }
    [[nodiscard]] fs::path database() const { return root_ / "project.sqlite"; }
private:
    fs::path root_;
};

class FixedIdGenerator final : public IIdGenerator {
public:
    std::string generate() override { return "job-event-integration"; }
};

class SequenceClock final : public IUtcClock {
public:
    explicit SequenceClock(std::deque<std::string> values) : values_{std::move(values)} {}
    std::string now_utc_iso8601() override {
        if (values_.empty()) throw std::runtime_error("Integration clock exhausted");
        std::string value = std::move(values_.front());
        values_.pop_front();
        return value;
    }
private:
    std::deque<std::string> values_;
};

[[nodiscard]] std::vector<WorkerProcessExit> await_exit(PlatformWorkerSupervisor& supervisor) {
    for (int attempt = 0; attempt < 300; ++attempt) {
        auto exits = supervisor.reap_exited();
        if (!exits.empty()) return exits;
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return {};
}

[[nodiscard]] bool end_to_end_contract(const fs::path& worker_executable) {
    TemporaryProject project;
    {
        SqliteConnection connection{project.database()};
        ProjectMigrationRunner migrations{connection};
        migrations.apply_pending();
        SqliteJobRepository repository{connection};
        FixedIdGenerator ids;
        SequenceClock clock{{
            "2026-08-06T21:00:00Z",
            "2026-08-06T21:01:00Z",
            "2026-08-06T21:02:00Z",
            "2026-08-06T21:03:00Z",
            "2026-08-06T21:04:00Z",
            "2026-08-06T21:05:00Z",
        }};
        JobService jobs{repository, ids, clock};
        auto job = jobs.create(CreateJobRequest{
            .analysis_id = std::nullopt,
            .pipeline_id = std::string{"bootstrap"},
            .pipeline_version = std::string{"1"},
            .priority = JobPriority::normal,
        });
        job = jobs.transition(job.id(), JobStatus::queued, 0.0, std::nullopt);
        job = jobs.transition(job.id(), JobStatus::preparing, 0.0, std::string{"launch"});
        if (job.revision() != 2) return false;

        WorkerEventIngestionSession session{jobs, std::string{job.id()}, job.revision()};
        PlatformWorkerSupervisor supervisor{worker_executable, project.root()};
        supervisor.launch(WorkerLaunchRequest{
            .job_id = std::string{job.id()},
            .analysis_id = job.analysis_id(),
            .pipeline_id = job.pipeline_id(),
            .pipeline_version = job.pipeline_version(),
            .priority = job.priority(),
            .job_revision = job.revision(),
            .execution_plan_path = std::nullopt,
        });

        const auto exits = await_exit(supervisor);
        if (exits.size() != 1U || exits.front().exit_code != 0 ||
            exits.front().events.size() != 5U ||
            !exits.front().diagnostics.empty() || !exits.front().protocol_issues.empty()) {
            return false;
        }
        for (const WorkerLifecycleEvent& event : exits.front().events) {
            static_cast<void>(session.ingest(event));
        }
        const auto finalization = session.finalize_process_exit(exits.front().exit_code);
        if (!finalization.matched_terminal_event || finalization.persisted_job.has_value()) {
            return false;
        }
    }

    SqliteConnection reopened{project.database()};
    ProjectMigrationRunner migrations{reopened};
    migrations.apply_pending();
    SqliteJobRepository repository{reopened};
    const auto stored = repository.find_by_id("job-event-integration");
    return stored.has_value() && stored->status() == JobStatus::completed &&
           stored->progress() == 1.0 && stored->revision() == 5 &&
           stored->active_step_id() == std::nullopt &&
           stored->started_at_utc().has_value() && stored->finished_at_utc().has_value();
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    if (argc != 2) {
        std::cerr << "Expected real worker executable path\n";
        return EXIT_FAILURE;
    }
    const fs::path worker = fs::canonical(path_from_utf8(argv[1]));
    if (!end_to_end_contract(worker)) {
        std::cerr << "Worker event pipeline/SQLite integration contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
