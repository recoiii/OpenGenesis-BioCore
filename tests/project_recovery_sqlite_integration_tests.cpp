#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/output_artifact_cleanup_service.hpp"
#include "biocore/application/project_recovery_service.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/storage_mode.hpp"
#include "biocore/infrastructure/filesystem_partial_output_cleaner.hpp"
#include "biocore/infrastructure/filesystem_quarantine_retention_store.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_job_repository.hpp"
#include "biocore/infrastructure/sqlite/sqlite_managed_file_repository.hpp"

namespace {
namespace fs = std::filesystem;
using namespace biocore;

class TempProject final {
public:
    TempProject() {
        root_ = fs::temp_directory_path() /
                ("biocore-recovery-sqlite-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root_ / ".biocore");
        fs::create_directories(root_ / "outputs");
        root_ = fs::canonical(root_);
    }
    ~TempProject() {
        std::error_code error;
        fs::remove_all(root_, error);
    }
    [[nodiscard]] const fs::path& root() const noexcept { return root_; }
private:
    fs::path root_;
};

class IDs final : public application::IIdGenerator {
public:
    std::string generate() override { return "unused"; }
};

class Clock final : public application::IUtcClock {
public:
    std::string now_utc_iso8601() override { return "2026-08-07T11:30:00Z"; }
};

[[nodiscard]] domain::Job running_job() {
    return domain::Job{
        "job-1", std::nullopt, std::string{"pipeline"}, std::string{"1"},
        domain::JobStatus::running, domain::JobPriority::normal, 0.2,
        std::string{"copy"}, "2026-08-07T10:00:00Z", "2026-08-07T10:10:00Z",
        std::string{"2026-08-07T10:05:00Z"}, std::nullopt, 4,
    };
}

[[nodiscard]] bool recovery_contract() {
    TempProject project;
    infrastructure::sqlite::SqliteConnection connection{project.root() / ".biocore" / "project.sqlite"};
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();

    infrastructure::sqlite::SqliteJobRepository jobs_repo{connection};
    infrastructure::sqlite::SqliteManagedFileRepository files_repo{connection};
    if (!jobs_repo.add(running_job())) return false;

    const std::string relative = "outputs/job-1--copy--result.out";
    const domain::ManagedFile file{
        "artifact-1", "job-1--copy--result.out", domain::StorageMode::generated_output,
        std::nullopt, (project.root() / relative).string(), relative, "txt", 5,
        std::nullopt, std::string{"sha256"}, std::string{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
        "2026-08-07T10:20:00Z", "2026-08-07T10:20:00Z",
    };
    const application::GeneratedOutputProvenance provenance{
        .job_id = "job-1",
        .step_id = "copy",
        .output_port = "result",
        .plugin_id = "org.biocore.demo",
        .plugin_version = "0.1.0",
        .module_id = "org.biocore.demo.copy",
        .file_type = "txt",
        .relative_project_path = relative,
        .step_progress = 0.65,
        .registered_at_utc = "2026-08-07T10:20:00Z",
    };
    if (!files_repo.add_generated_output(file, provenance)) return false;
    if (files_repo.latest_generated_output_progress("job-1") != std::optional<double>{0.65}) {
        return false;
    }

    IDs ids;
    Clock clock;
    application::JobService jobs{jobs_repo, ids, clock};
    infrastructure::FilesystemPartialOutputCleaner cleaner{project.root()};
    application::OutputArtifactCleanupService cleanup{files_repo, cleaner};
    infrastructure::FilesystemQuarantineRetentionStore retention{project.root()};
    application::ProjectRecoveryService recovery{jobs, files_repo, &cleanup, &retention};

    const auto first = recovery.recover();
    const auto recovered = jobs_repo.find_by_id("job-1");
    if (first.recovered_jobs.size() != 1U || !first.issues.empty() || !recovered.has_value() ||
        recovered->status() != domain::JobStatus::interrupted || recovered->progress() != 0.65 ||
        !recovered->failure().has_value() ||
        recovered->failure()->kind() != domain::JobFailureKind::startup_recovery ||
        recovered->revision() != 5) {
        return false;
    }

    const auto second = recovery.recover();
    const auto again = jobs_repo.find_by_id("job-1");
    return second.recovered_jobs.empty() && second.issues.empty() && again.has_value() &&
           again->revision() == recovered->revision() && again->progress() == 0.65;
}

}  // namespace

int main() {
    if (!recovery_contract()) {
        std::cerr << "Project recovery SQLite integration failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
