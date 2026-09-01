#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "biocore/application/job_scheduler.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/pipeline_preparation_service.hpp"
#include "biocore/application/worker_runtime.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/job_status.hpp"
#include "biocore/domain/pipeline_definition.hpp"
#include "biocore/domain/pipeline_step.hpp"
#include "biocore/infrastructure/filesystem_plugin_registry.hpp"
#include "biocore/infrastructure/json_execution_plan_store.hpp"
#include "biocore/infrastructure/monotonic_clock.hpp"
#include "biocore/infrastructure/platform_worker_supervisor.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_job_repository.hpp"
#include "biocore/infrastructure/sqlite/sqlite_prepared_job_store.hpp"
#include "biocore/infrastructure/system_clock.hpp"
#include "biocore/infrastructure/uuid_v4_generator.hpp"

namespace {

namespace fs = std::filesystem;
using namespace std::chrono_literals;

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
        const auto unique = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        root_ = fs::temp_directory_path() / ("biocore-worker-runtime-" + unique);
        fs::create_directories(root_ / ".biocore" / "runtime");
        fs::create_directories(root_ / "outputs");
        root_ = fs::canonical(root_);
    }
    ~TemporaryProject() { std::error_code error; fs::remove_all(root_, error); }
    [[nodiscard]] const fs::path& root() const noexcept { return root_; }
    [[nodiscard]] fs::path database() const { return root_ / ".biocore" / "project.sqlite"; }
private:
    fs::path root_;
};

class UnusedIdGenerator final : public biocore::application::IIdGenerator {
public:
    std::string generate() override { throw std::runtime_error("Runtime integration must not generate IDs"); }
};

class SequenceClock final : public biocore::application::IUtcClock {
public:
    explicit SequenceClock(std::deque<std::string> values) : values_{std::move(values)} {}
    std::string now_utc_iso8601() override {
        if (values_.empty()) throw std::runtime_error("Runtime integration clock exhausted");
        std::string value = std::move(values_.front()); values_.pop_front(); return value;
    }
private:
    std::deque<std::string> values_;
};

[[nodiscard]] biocore::domain::PipelineDefinition demo_definition() {
    return biocore::domain::PipelineDefinition{
        2U, "org.biocore.demo.validation", "Demo File Validation", "0.1.0",
        {
            {"validate", "org.biocore.demo.validate", "0.1.0", {}, 0.2},
            {"scan", "org.biocore.demo.scan", "0.1.0", {"validate"}, 0.6},
            {"report", "org.biocore.demo.report", "0.1.0", {"scan"}, 0.2},
        },
    };
}

[[nodiscard]] bool autonomous_runtime_contract(
    const fs::path& worker_executable,
    const fs::path& plugin_root
) {
    TemporaryProject project;
    {
        biocore::infrastructure::sqlite::SqliteConnection connection{project.database()};
        biocore::infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
        migrations.apply_pending();
        biocore::infrastructure::sqlite::SqliteJobRepository repository{connection};
        biocore::infrastructure::sqlite::SqlitePreparedJobStore prepared_jobs{connection};
        biocore::infrastructure::FilesystemPluginRegistry plugins{{plugin_root}};
        if (plugins.refresh().loaded_modules < 3U) return false;
        biocore::infrastructure::JsonExecutionPlanStore plan_store{project.root()};
        biocore::application::PipelinePreparationService preparation{plan_store, plugins};
        const auto prepared = preparation.prepare(demo_definition(), "job-runtime-integration", 2);

        biocore::domain::Job queued{
            "job-runtime-integration", std::nullopt, "org.biocore.demo.validation", "0.1.0",
            biocore::domain::JobStatus::queued, biocore::domain::JobPriority::normal, 0.0,
            std::nullopt, "2026-08-07T00:00:00Z", "2026-08-07T00:00:01Z",
            std::nullopt, std::nullopt, 1
        };
        if (!prepared_jobs.add_prepared_job(queued, {
                .job_id = "job-runtime-integration",
                .launch_revision = 2,
                .pipeline_id = "org.biocore.demo.validation",
                .pipeline_version = "0.1.0",
                .execution_plan_path = prepared.snapshot_path,
                .prepared_at_utc = "2026-08-07T00:00:01Z",
            })) return false;

        UnusedIdGenerator ids;
        SequenceClock utc{{
            "2026-08-07T00:00:02Z", "2026-08-07T00:00:03Z", "2026-08-07T00:00:04Z",
            "2026-08-07T00:00:05Z", "2026-08-07T00:00:06Z", "2026-08-07T00:00:07Z",
            "2026-08-07T00:00:08Z", "2026-08-07T00:00:09Z", "2026-08-07T00:00:10Z",
        }};
        biocore::application::JobService jobs{repository, ids, utc};
        biocore::infrastructure::PlatformWorkerSupervisor supervisor{worker_executable, project.root()};
        biocore::application::JobScheduler scheduler{jobs, prepared_jobs, supervisor, 1U};
        biocore::infrastructure::MonotonicClock monotonic;
        biocore::application::WorkerRuntime runtime{scheduler, jobs, supervisor, monotonic, {10ms, 2s}};

        runtime.start();
        bool completed_and_reaped = false;
        for (int attempt = 0; attempt < 500; ++attempt) {
            const auto current = jobs.find_by_id("job-runtime-integration");
            if (current.has_value() && current->status() == biocore::domain::JobStatus::completed &&
                runtime.active_session_count() == 0U) {
                completed_and_reaped = true; break;
            }
            std::this_thread::sleep_for(10ms);
        }
        runtime.stop();
        if (!completed_and_reaped || runtime.running() || !runtime.drain_background_issues().empty() ||
            !runtime.drain_background_diagnostics().empty() || !runtime.drain_background_protocol_issues().empty()) {
            return false;
        }
    }

    biocore::infrastructure::sqlite::SqliteConnection reopened{project.database()};
    biocore::infrastructure::sqlite::ProjectMigrationRunner migrations{reopened};
    migrations.apply_pending();
    biocore::infrastructure::sqlite::SqliteJobRepository repository{reopened};
    const auto stored = repository.find_by_id("job-runtime-integration");
    return stored.has_value() && stored->status() == biocore::domain::JobStatus::completed &&
           stored->progress() == 1.0 && stored->revision() >= 5 &&
           stored->active_step_id() == std::nullopt;
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    if (argc != 3) {
        std::cerr << "Expected real worker executable and plugin root\n";
        return EXIT_FAILURE;
    }
    const fs::path worker = fs::canonical(path_from_utf8(argv[1]));
    const fs::path plugins = fs::canonical(path_from_utf8(argv[2]));
    if (!autonomous_runtime_contract(worker, plugins)) {
        std::cerr << "Autonomous worker runtime/SQLite prepared-plan integration contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
