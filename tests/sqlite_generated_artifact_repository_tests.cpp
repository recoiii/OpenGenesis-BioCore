#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "biocore/application/generated_output_artifact.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/storage_mode.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_managed_file_repository.hpp"

namespace {
using namespace biocore;

[[nodiscard]] domain::ManagedFile file(std::string id, std::string relative) {
    return domain::ManagedFile{
        std::move(id), "result.out", domain::StorageMode::generated_output, std::nullopt,
        std::string{"/project/"} + relative, relative, "txt", 11, std::nullopt,
        std::string{"sha256"}, std::string{"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}, "2026-08-07T08:00:00Z", "2026-08-07T08:00:00Z"
    };
}

[[nodiscard]] application::GeneratedOutputProvenance provenance(std::string job = "job-1") {
    return application::GeneratedOutputProvenance{
        .job_id = std::move(job), .step_id = "copy", .output_port = "result",
        .plugin_id = "org.biocore.demo", .plugin_version = "0.1.0",
        .module_id = "org.biocore.demo.copy", .file_type = "txt",
        .relative_project_path = "outputs/job-1--copy--result.out",
        .step_progress = 0.5,
        .registered_at_utc = "2026-08-07T08:00:00Z",
    };
}

[[nodiscard]] application::GeneratedOutputArtifact artifact(
    std::string id,
    std::string port,
    std::string relative
) {
    auto generated_file = file(std::move(id), relative);
    auto generated_provenance = provenance();
    generated_provenance.output_port = std::move(port);
    generated_provenance.relative_project_path = std::move(relative);
    return {std::move(generated_file), std::move(generated_provenance)};
}

[[nodiscard]] int count_rows(infrastructure::sqlite::SqliteConnection& connection, const char* sql) {
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(connection.native_handle(), sql, -1, &statement, nullptr) != SQLITE_OK) return -1;
    const int step = sqlite3_step(statement);
    const int value = step == SQLITE_ROW ? sqlite3_column_int(statement, 0) : -1;
    sqlite3_finalize(statement);
    return value;
}

[[nodiscard]] bool contract() {
    infrastructure::sqlite::SqliteConnection connection{std::filesystem::path{":memory:"}};
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    connection.execute(
        "INSERT INTO jobs(id,status,priority,progress,created_at_utc,updated_at_utc,revision) "
        "VALUES('job-1','running','normal',0.5,'c','u',0);"
    );
    infrastructure::sqlite::SqliteManagedFileRepository repository{connection};

    auto first_file = file("artifact-1", "outputs/job-1--copy--result.out");
    auto first_provenance = provenance();
    if (!repository.add_generated_output(first_file, first_provenance)) return false;
    const auto found = repository.find_generated_output("job-1", "copy", "result");
    if (!found.has_value() || found->file.id() != "artifact-1" ||
        found->provenance.plugin_version != "0.1.0" ||
        repository.list_generated_outputs("job-1").size() != 1U) return false;

    auto duplicate = file("artifact-2", "outputs/job-1--copy--other.out");
    auto duplicate_provenance = first_provenance;
    duplicate_provenance.relative_project_path = "outputs/job-1--copy--other.out";
    if (repository.add_generated_output(duplicate, duplicate_provenance)) return false;
    if (repository.find_by_id("artifact-2").has_value()) return false;

    auto foreign = file("artifact-3", "outputs/job-x--copy--result.out");
    auto foreign_provenance = provenance("missing-job");
    foreign_provenance.relative_project_path = "outputs/job-x--copy--result.out";
    if (repository.add_generated_output(foreign, foreign_provenance)) return false;
    if (repository.find_by_id("artifact-3").has_value()) return false;

    return count_rows(connection, "SELECT COUNT(*) FROM managed_files;") == 1 &&
           count_rows(connection, "SELECT COUNT(*) FROM generated_artifacts;") == 1;
}

[[nodiscard]] bool batch_rollback_contract() {
    infrastructure::sqlite::SqliteConnection connection{std::filesystem::path{":memory:"}};
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    connection.execute(
        "INSERT INTO jobs(id,status,priority,progress,created_at_utc,updated_at_utc,revision) "
        "VALUES('job-1','running','normal',0.5,'c','u',0);"
    );
    infrastructure::sqlite::SqliteManagedFileRepository repository{connection};

    const std::string conflicting_path = "outputs/job-1--copy--metrics.out";
    if (!repository.add(file("existing-file", conflicting_path))) return false;

    const std::vector batch{
        artifact("artifact-a", "result", "outputs/job-1--copy--result.out"),
        artifact("artifact-b", "metrics", conflicting_path),
    };
    if (repository.add_generated_outputs_batch(batch)) return false;

    return !repository.find_by_id("artifact-a").has_value() &&
           !repository.find_by_id("artifact-b").has_value() &&
           !repository.find_generated_output("job-1", "copy", "result").has_value() &&
           !repository.find_generated_output("job-1", "copy", "metrics").has_value() &&
           count_rows(connection, "SELECT COUNT(*) FROM managed_files;") == 1 &&
           count_rows(connection, "SELECT COUNT(*) FROM generated_artifacts;") == 0;
}
}

int main() {
    if (!contract() || !batch_rollback_contract()) {
        std::cerr << "SQLite generated artifact repository tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
