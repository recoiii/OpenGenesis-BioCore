#include <sqlite3.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <stdexcept>
#include <utility>

#include "biocore/application/i_job_repository.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/job_status.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"
#include "biocore/infrastructure/sqlite/sqlite_job_repository.hpp"

namespace {

using biocore::application::IJobRepository;
using biocore::domain::Job;
using biocore::domain::JobPriority;
using biocore::domain::JobStatus;
using biocore::infrastructure::sqlite::ProjectMigrationRunner;
using biocore::infrastructure::sqlite::SqliteConnection;
using biocore::infrastructure::sqlite::SqliteError;
using biocore::infrastructure::sqlite::SqliteJobRepository;

class TemporaryDatabase final {
public:
    TemporaryDatabase() {
        const auto unique_value = std::chrono::steady_clock::now().time_since_epoch().count();
        directory_ = std::filesystem::temp_directory_path() /
                     ("biocore-job-repository-" + std::to_string(unique_value));
        std::filesystem::create_directory(directory_);
        path_ = directory_ / std::filesystem::path{u8"işler.sqlite"};
    }

    ~TemporaryDatabase() {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    TemporaryDatabase(const TemporaryDatabase&) = delete;
    TemporaryDatabase& operator=(const TemporaryDatabase&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path directory_;
    std::filesystem::path path_;
};

[[nodiscard]] Job make_job(
    std::string id,
    std::string created_at,
    const JobPriority priority = JobPriority::normal
) {
    return Job{
        std::move(id),
        std::string{"analiz-'α"},
        std::string{"org.biocore.örnek"},
        std::string{"0.1.0"},
        JobStatus::draft,
        priority,
        0.0,
        std::nullopt,
        created_at,
        created_at,
        std::nullopt,
        std::nullopt,
        0,
    };
}

[[nodiscard]] bool matches_metadata(const Job& job) {
    return job.analysis_id().has_value() && *job.analysis_id() == "analiz-'α" &&
           job.pipeline_id().has_value() && *job.pipeline_id() == "org.biocore.örnek" &&
           job.pipeline_version().has_value() && *job.pipeline_version() == "0.1.0";
}

[[nodiscard]] bool run_contract(IJobRepository& repository) {
    Job second = make_job("job-b", "2026-08-06T20:01:00Z", JobPriority::low);
    Job first = make_job("job-'a", "2026-08-06T20:00:00Z", JobPriority::high);

    if (repository.find_by_id(first.id()).has_value() || !repository.add(second) ||
        !repository.add(first) || repository.add(first)) {
        std::cerr << "Job repository insertion contract failed\n";
        return false;
    }

    const auto fetched = repository.find_by_id(first.id());
    if (!fetched.has_value() || fetched->id() != first.id() ||
        fetched->priority() != JobPriority::high || !matches_metadata(*fetched) ||
        fetched->revision() != 0) {
        std::cerr << "Job repository lookup contract failed\n";
        return false;
    }

    const auto jobs = repository.list();
    if (jobs.size() != 2U || jobs[0].id() != first.id() || jobs[1].id() != second.id()) {
        std::cerr << "Job repository ordering contract failed\n";
        return false;
    }

    Job queued = *fetched;
    queued.transition_to(JobStatus::queued, 0.0, std::nullopt, "2026-08-06T20:02:00Z");
    if (!repository.update_runtime_state(queued, 0) ||
        repository.update_runtime_state(queued, 0)) {
        std::cerr << "Job repository optimistic update contract failed\n";
        return false;
    }

    Job updated = queued;
    updated.transition_to(
        JobStatus::preparing,
        0.2,
        std::string{"hazırlık"},
        "2026-08-06T20:03:00Z"
    );
    if (!repository.update_runtime_state(updated, 1)) {
        std::cerr << "Job repository second runtime update failed\n";
        return false;
    }

    const auto stored = repository.find_by_id(first.id());
    if (!stored.has_value() || stored->status() != JobStatus::preparing ||
        stored->progress() != 0.2 || stored->revision() != 2 ||
        !stored->active_step_id().has_value() || *stored->active_step_id() != "hazırlık" ||
        !stored->started_at_utc().has_value() ||
        *stored->started_at_utc() != "2026-08-06T20:03:00Z" ||
        stored->created_at_utc() != "2026-08-06T20:00:00Z" ||
        stored->priority() != JobPriority::high || !matches_metadata(*stored)) {
        std::cerr << "Job repository runtime persistence contract failed\n";
        return false;
    }

    Job running = *stored;
    running.transition_to(
        JobStatus::running,
        0.8,
        std::string{"çalışıyor"},
        "2026-08-06T20:04:00Z"
    );
    if (!repository.update_runtime_state(running, 2)) {
        std::cerr << "Job repository running update failed\n";
        return false;
    }

    Job completed = running;
    completed.transition_to(
        JobStatus::completed,
        1.0,
        std::string{"ignored"},
        "2026-08-06T20:05:00Z"
    );
    if (!repository.update_runtime_state(completed, 3)) {
        std::cerr << "Job repository terminal update failed\n";
        return false;
    }

    const auto terminal = repository.find_by_id(first.id());
    if (!terminal.has_value() || terminal->status() != JobStatus::completed ||
        terminal->revision() != 4 || terminal->progress() != 1.0 ||
        terminal->active_step_id().has_value() || !terminal->finished_at_utc().has_value() ||
        *terminal->finished_at_utc() != "2026-08-06T20:05:00Z") {
        std::cerr << "Job repository terminal read contract failed\n";
        return false;
    }

    try {
        static_cast<void>(repository.update_runtime_state(completed, 2));
        std::cerr << "Job repository accepted a revision jump\n";
        return false;
    } catch (const std::invalid_argument&) {
    }

    Job missing = make_job("missing", "2026-08-06T20:06:00Z");
    missing.transition_to(JobStatus::queued, 0.0, std::nullopt, "2026-08-06T20:07:00Z");
    return !repository.update_runtime_state(missing, 0);
}

[[nodiscard]] bool optional_metadata_contract() {
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    SqliteJobRepository repository{connection};

    Job job{
        "job-minimal",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        JobStatus::draft,
        JobPriority::normal,
        0.0,
        std::nullopt,
        "2026-08-06T20:10:00Z",
        "2026-08-06T20:10:00Z",
        std::nullopt,
        std::nullopt,
        0,
    };
    if (!repository.add(job)) {
        return false;
    }

    const auto stored = repository.find_by_id(job.id());
    return stored.has_value() && !stored->analysis_id().has_value() &&
           !stored->pipeline_id().has_value() && !stored->pipeline_version().has_value();
}


[[nodiscard]] bool rejects_corrupted_database_rows() {
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    connection.execute(
        "INSERT INTO jobs(id, status, priority, progress, created_at_utc, updated_at_utc) "
        "VALUES ('corrupt-running', 'running', 'normal', 0.5, 'created', 'updated');"
    );

    SqliteJobRepository repository{connection};
    try {
        static_cast<void>(repository.find_by_id("corrupt-running"));
    } catch (const SqliteError& error) {
        return error.result_code() == SQLITE_MISMATCH;
    }
    return false;
}

[[nodiscard]] bool disk_persistence_contract() {
    TemporaryDatabase temporary;
    {
        SqliteConnection connection{temporary.path()};
        ProjectMigrationRunner migrations{connection};
        migrations.apply_pending();
        SqliteJobRepository repository{connection};
        if (!repository.add(make_job("kalıcı-job", "2026-08-06T20:20:00Z"))) {
            return false;
        }
    }

    {
        SqliteConnection connection{temporary.path()};
        ProjectMigrationRunner migrations{connection};
        migrations.apply_pending();
        SqliteJobRepository repository{connection};
        const auto job = repository.find_by_id("kalıcı-job");
        return job.has_value() && matches_metadata(*job) && job->revision() == 0;
    }
}

}  // namespace

int main() {
    {
        SqliteConnection connection{std::filesystem::path{":memory:"}};
        ProjectMigrationRunner migrations{connection};
        migrations.apply_pending();
        SqliteJobRepository repository{connection};
        if (!run_contract(repository)) {
            return EXIT_FAILURE;
        }
    }

    if (!optional_metadata_contract()) {
        std::cerr << "Job repository optional metadata contract failed\n";
        return EXIT_FAILURE;
    }
    if (!rejects_corrupted_database_rows()) {
        std::cerr << "Job repository corrupted-row contract failed\n";
        return EXIT_FAILURE;
    }
    if (!disk_persistence_contract()) {
        std::cerr << "Job repository disk persistence contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
