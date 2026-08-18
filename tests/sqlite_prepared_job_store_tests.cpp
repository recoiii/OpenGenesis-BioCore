#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>

#include "biocore/application/i_prepared_job_store.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/job_status.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_job_repository.hpp"
#include "biocore/infrastructure/sqlite/sqlite_prepared_job_store.hpp"

namespace {

biocore::domain::Job queued(std::string id, std::int64_t revision = 1) {
    return {std::move(id), std::nullopt, "pipe", "1.0", biocore::domain::JobStatus::queued,
            biocore::domain::JobPriority::normal, 0.0, std::nullopt,
            "2026-08-07T00:00:00Z", "2026-08-07T00:00:01Z", std::nullopt, std::nullopt, revision};
}

biocore::application::PreparedJobExecution execution(std::string job, std::string path) {
    return {std::move(job), 2, "pipe", "1.0", std::move(path), "2026-08-07T00:00:01Z"};
}

void require(bool value, std::string_view message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

}  // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
        ("biocore-prepared-store-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    const auto db = root / "project.sqlite";
    biocore::infrastructure::sqlite::SqliteConnection connection{db};
    biocore::infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    require(migrations.current_version() == 6, "schema v6 required");
    biocore::infrastructure::sqlite::SqlitePreparedJobStore prepared{connection};
    biocore::infrastructure::sqlite::SqliteJobRepository jobs{connection};

    require(prepared.add_prepared_job(queued("job-a"), execution("job-a", "/project/.biocore/runtime/jobs/job-a/execution-plan-r2.json")), "prepared insert");
    const auto found = prepared.find_execution("job-a");
    require(found.has_value() && found->launch_revision == 2 && found->pipeline_id == "pipe", "prepared lookup");
    require(!prepared.add_prepared_job(queued("job-a"), execution("job-a", "/another.json")), "duplicate id must return false");

    bool conflict = false;
    try {
        static_cast<void>(prepared.add_prepared_job(queued("job-b"), execution("job-b", "/project/.biocore/runtime/jobs/job-a/execution-plan-r2.json")));
    } catch (...) { conflict = true; }
    require(conflict, "duplicate execution path must fail");
    require(!jobs.find_by_id("job-b").has_value(), "execution failure must rollback sibling Job insert");
    {
        biocore::infrastructure::sqlite::SqliteConnection second_connection{db};
        biocore::infrastructure::sqlite::SqliteJobRepository second_jobs{second_connection};
        require(second_jobs.add(queued("job-after-rollback")),
                "prepared-job failure must release the write transaction for another SQLite connection");
    }

    require(jobs.add(queued("job-c")), "direct-SQL trigger fixture job insert");
    bool trigger_rejected = false;
    try {
        connection.execute(R"sql(
            INSERT INTO job_execution_plans(
                job_id, launch_revision, pipeline_id, pipeline_version, execution_plan_path, prepared_at_utc
            ) VALUES (
                'job-c', 99, 'pipe', '1.0', '/project/.biocore/runtime/jobs/job-c/execution-plan-r99.json',
                '2026-08-07T00:00:01Z'
            );
        )sql");
    } catch (...) {
        trigger_rejected = true;
    }
    require(trigger_rejected, "schema v6 trigger must reject mismatched direct prepared-plan insertion");
    require(!prepared.find_execution("job-c").has_value(), "rejected direct insertion must leave no prepared association");

    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::cout << "SQLite prepared job store tests passed\n";
    return 0;
}
