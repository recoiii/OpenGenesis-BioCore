#include <sqlite3.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "biocore/domain/job_status.hpp"
#include "biocore/domain/project.hpp"
#include "biocore/infrastructure/sqlite/catalog_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/project_database_initializer.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"

namespace {

using biocore::domain::JobStatus;
using biocore::domain::Project;
using biocore::domain::to_string;
using biocore::infrastructure::sqlite::CatalogMigrationRunner;
using biocore::infrastructure::sqlite::ProjectDatabaseInitializer;
using biocore::infrastructure::sqlite::ProjectMigrationRunner;
using biocore::infrastructure::sqlite::SqliteConnection;
using biocore::infrastructure::sqlite::SqliteError;
using biocore::infrastructure::sqlite::latest_project_schema_version;

class TemporaryDatabase final {
public:
    TemporaryDatabase() {
        const auto unique_value = std::chrono::steady_clock::now().time_since_epoch().count();
        directory_ = std::filesystem::temp_directory_path() /
                     ("biocore-project-database-test-" + std::to_string(unique_value));
        std::filesystem::create_directory(directory_);
        path_ = directory_ / std::filesystem::path{u8"proje-yerel-ü.sqlite"};
    }

    ~TemporaryDatabase() {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path directory_;
    std::filesystem::path path_;
};

class Statement final {
public:
    Statement(SqliteConnection& connection, const char* sql)
        : database_{connection.native_handle()} {
        const int result = sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr);
        if (result != SQLITE_OK) {
            throw SqliteError{result, std::string{"Unable to prepare test query: "} + sqlite3_errmsg(database_)};
        }
    }

    ~Statement() {
        if (statement_ != nullptr) {
            sqlite3_finalize(statement_);
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    [[nodiscard]] int step() {
        return sqlite3_step(statement_);
    }

    [[nodiscard]] int integer(const int column) const {
        return sqlite3_column_int(statement_, column);
    }

    [[nodiscard]] std::string text(const int column) const {
        const auto* value = sqlite3_column_text(statement_, column);
        if (value == nullptr) {
            throw SqliteError{SQLITE_MISMATCH, "Unexpected NULL in test query"};
        }
        const int size = sqlite3_column_bytes(statement_, column);
        return {reinterpret_cast<const char*>(value), static_cast<std::size_t>(size)};
    }

private:
    sqlite3* database_;
    sqlite3_stmt* statement_{nullptr};
};

[[nodiscard]] bool table_exists(SqliteConnection& connection, const std::string_view table_name) {
    sqlite3_stmt* raw_statement = nullptr;
    sqlite3* const database = connection.native_handle();
    constexpr const char* sql =
        "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = ?;";
    const int prepare_result = sqlite3_prepare_v2(database, sql, -1, &raw_statement, nullptr);
    if (prepare_result != SQLITE_OK) {
        throw SqliteError{prepare_result, "Unable to prepare table-existence query"};
    }
    const int bind_table_result = sqlite3_bind_text(
        raw_statement,
        1,
        table_name.data(),
        static_cast<int>(table_name.size()),
        SQLITE_TRANSIENT
    );
    if (bind_table_result != SQLITE_OK) {
        sqlite3_finalize(raw_statement);
        throw SqliteError{bind_table_result, "Unable to bind table-existence query"};
    }
    const int step_result = sqlite3_step(raw_statement);
    if (step_result != SQLITE_ROW) {
        sqlite3_finalize(raw_statement);
        throw SqliteError{step_result, "Unable to execute table-existence query"};
    }
    const bool exists = sqlite3_column_int(raw_statement, 0) == 1;
    const int finalize_result = sqlite3_finalize(raw_statement);
    if (finalize_result != SQLITE_OK) {
        throw SqliteError{finalize_result, "Unable to finalize table-existence query"};
    }
    return exists;
}

[[nodiscard]] int scalar_integer(SqliteConnection& connection, const char* sql) {
    Statement statement{connection, sql};
    if (statement.step() != SQLITE_ROW) {
        throw SqliteError{SQLITE_ERROR, "Integer query did not return a row"};
    }
    return statement.integer(0);
}

[[nodiscard]] std::string scalar_text(SqliteConnection& connection, const char* sql) {
    Statement statement{connection, sql};
    if (statement.step() != SQLITE_ROW) {
        throw SqliteError{SQLITE_ERROR, "Text query did not return a row"};
    }
    return statement.text(0);
}

[[nodiscard]] bool statement_throws(SqliteConnection& connection, const std::string_view sql) {
    try {
        connection.execute(sql);
    } catch (const SqliteError&) {
        return true;
    }
    return false;
}

[[nodiscard]] Project make_project() {
    return Project{
        "project-'yerel",
        "Türkçe proje verisi",
        "/projects/kanonik-örnek",
        "2026-08-06T21:10:00Z",
        "2026-08-06T21:10:00Z",
    };
}

[[nodiscard]] bool migration_is_idempotent_and_creates_core_schema() {
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    migrations.apply_pending();

    constexpr std::string_view expected_tables[]{
        "schema_migrations",
        "project_metadata",
        "managed_files",
        "generated_artifacts",
        "jobs",
        "settings",
    };
    for (const std::string_view table : expected_tables) {
        if (!table_exists(connection, table)) {
            return false;
        }
    }

    if (migrations.current_version() != latest_project_schema_version ||
        scalar_integer(connection, "SELECT COUNT(*) FROM schema_migrations;") != latest_project_schema_version) {
        return false;
    }

    if (!statement_throws(
            connection,
            "INSERT INTO managed_files(id, display_name, storage_mode, original_path, file_type, "
            "size_bytes, created_at_utc, updated_at_utc) VALUES "
            "('file-1', 'Invalid', 'unknown', '/tmp/a', 'FASTQ', 1, 't', 't');"
        ) ||
        !statement_throws(
            connection,
            "INSERT INTO jobs(id, status, created_at_utc, updated_at_utc) "
            "VALUES ('job-1', 'skipped', 't', 't');"
        ) ||
        !statement_throws(
            connection,
            "INSERT INTO jobs(id, status, progress, created_at_utc, updated_at_utc) "
            "VALUES ('job-2', 'queued', 1.5, 't', 't');"
        ) ||
        !statement_throws(
            connection,
            "INSERT INTO settings(key, value_json, updated_at_utc) VALUES ('   ', '{}', 't');"
        ) ||
        !statement_throws(
            connection,
            "INSERT INTO jobs(id, status, analysis_id, created_at_utc, updated_at_utc) "
            "VALUES ('job-blank-analysis', 'draft', '   ', 't', 't');"
        ) ||
        !statement_throws(
            connection,
            "INSERT INTO jobs(id, status, revision, created_at_utc, updated_at_utc) "
            "VALUES ('job-negative-revision', 'draft', -1, 't', 't');"
        )) {
        return false;
    }

    if (!statement_throws(
            connection,
            "INSERT INTO managed_files(id, display_name, storage_mode, managed_path, "
            "relative_project_path, file_type, size_bytes, created_at_utc, updated_at_utc) "
            "VALUES ('generated-no-hash', 'Output', 'generated_output', '/project/outputs/x', "
            "'outputs/x', 'txt', 1, 't', 't');"
        ) ||
        !statement_throws(
            connection,
            "INSERT INTO managed_files(id, display_name, storage_mode, managed_path, "
            "relative_project_path, file_type, size_bytes, checksum_algorithm, checksum_value, "
            "created_at_utc, updated_at_utc) VALUES ('generated-bad-hash', 'Output', "
            "'generated_output', '/project/outputs/y', 'outputs/y', 'txt', 1, 'sha256', "
            "'AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA', 't', 't');"
        )) {
        return false;
    }

    constexpr std::array<std::string_view, 5> storage_modes{
        "managed_copy",
        "external_reference",
        "managed_move",
        "generated_output",
        "temporary",
    };
    for (std::size_t index = 0; index < storage_modes.size(); ++index) {
        connection.execute(
            "INSERT INTO managed_files(id, display_name, storage_mode, original_path, "
            "relative_project_path, file_type, size_bytes, checksum_algorithm, checksum_value, "
            "created_at_utc, updated_at_utc) VALUES ('file-" +
            std::to_string(index) + "', 'Reads', '" + std::string{storage_modes[index]} +
            "', '/tmp/reads.fastq', 'inputs/reads-" + std::to_string(index) +
            ".fastq', 'FASTQ', 42, 'sha256', '" + std::string(64U, 'a') + "', 't', 't');"
        );
    }

    if (!statement_throws(
            connection,
            "INSERT INTO managed_files(id, display_name, storage_mode, original_path, "
            "relative_project_path, file_type, size_bytes, created_at_utc, updated_at_utc) "
            "VALUES ('file-duplicate-path', 'Duplicate', 'managed_copy', '/tmp/other.fastq', "
            "'inputs/reads-0.fastq', 'FASTQ', 1, 't', 't');"
        )) {
        return false;
    }
    if (!statement_throws(
            connection,
            "UPDATE managed_files SET checksum_value = 'bad' WHERE storage_mode = 'generated_output';"
        )) {
        return false;
    }

    constexpr std::array<JobStatus, 10> job_statuses{
        JobStatus::draft,
        JobStatus::queued,
        JobStatus::preparing,
        JobStatus::running,
        JobStatus::paused,
        JobStatus::cancelling,
        JobStatus::cancelled,
        JobStatus::completed,
        JobStatus::failed,
        JobStatus::interrupted,
    };
    for (std::size_t index = 0; index < job_statuses.size(); ++index) {
        const bool has_failure = job_statuses[index] == JobStatus::failed ||
                                 job_statuses[index] == JobStatus::interrupted;
        connection.execute(
            "INSERT INTO jobs(id, status, progress, created_at_utc, updated_at_utc" +
            std::string{has_failure
                ? ", failure_kind, failure_message, failure_recorded_at_utc"
                : ""} +
            ") VALUES ('job-" + std::to_string(index) + "', '" +
            std::string{to_string(job_statuses[index])} + "', 0.25, 't', 't'" +
            std::string{has_failure
                ? ", 'legacy_terminal_state', 'test terminal evidence', 't'"
                : ""} +
            ");"
        );
    }

    connection.execute(
        "INSERT INTO settings(key, value_json, updated_at_utc) "
        "VALUES ('ui.theme', '{\"mode\":\"system\"}', 't');"
    );

    return scalar_integer(connection, "SELECT COUNT(*) FROM managed_files;") ==
               static_cast<int>(storage_modes.size()) &&
           scalar_integer(connection, "SELECT COUNT(*) FROM jobs;") ==
               static_cast<int>(job_statuses.size()) &&
           scalar_integer(connection, "SELECT COUNT(*) FROM settings;") == 1;
}


[[nodiscard]] bool upgrades_version_one_jobs_without_data_loss() {
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    connection.execute(R"sql(
        CREATE TABLE schema_migrations (
            version INTEGER PRIMARY KEY NOT NULL,
            name TEXT NOT NULL,
            applied_at_utc TEXT NOT NULL
        );
        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (1, 'create_project_core_tables', '2026-08-06T21:10:00Z');

        CREATE TABLE managed_files (
            id TEXT PRIMARY KEY NOT NULL,
            display_name TEXT NOT NULL DEFAULT 'legacy',
            storage_mode TEXT NOT NULL DEFAULT 'managed_copy',
            original_path TEXT,
            managed_path TEXT,
            relative_project_path TEXT,
            file_type TEXT NOT NULL DEFAULT 'unknown',
            size_bytes INTEGER NOT NULL DEFAULT 0,
            modified_at_utc TEXT,
            checksum_algorithm TEXT,
            checksum_value TEXT,
            created_at_utc TEXT NOT NULL DEFAULT 'c',
            updated_at_utc TEXT NOT NULL DEFAULT 'u'
        );

        CREATE TABLE jobs (
            id TEXT PRIMARY KEY NOT NULL,
            status TEXT NOT NULL,
            priority TEXT NOT NULL DEFAULT 'normal',
            progress REAL NOT NULL DEFAULT 0.0,
            active_step_id TEXT,
            created_at_utc TEXT NOT NULL,
            updated_at_utc TEXT NOT NULL
        );
        INSERT INTO jobs(id, status, priority, progress, created_at_utc, updated_at_utc)
        VALUES ('legacy-job', 'queued', 'high', 0.25, 'created', 'updated');
        INSERT INTO jobs(
            id, status, priority, progress, active_step_id, created_at_utc, updated_at_utc
        ) VALUES (
            'legacy-completed', 'completed', 'normal', 0.25, 'old-step', 'created-2', 'updated-2'
        );
    )sql");

    ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();

    return migrations.current_version() == latest_project_schema_version &&
           scalar_integer(connection, "SELECT COUNT(*) FROM schema_migrations;") == latest_project_schema_version &&
           scalar_integer(
               connection,
               "SELECT COUNT(*) FROM jobs WHERE id = 'legacy-job' AND analysis_id IS NULL "
               "AND pipeline_id IS NULL AND pipeline_version IS NULL "
               "AND started_at_utc IS NULL AND finished_at_utc IS NULL AND revision = 0;"
           ) == 1 &&
           scalar_integer(
               connection,
               "SELECT COUNT(*) FROM jobs WHERE id = 'legacy-completed' "
               "AND progress = 1.0 AND active_step_id IS NULL "
               "AND started_at_utc = 'updated-2' AND finished_at_utc = 'updated-2' "
               "AND revision = 0;"
           ) == 1;
}

[[nodiscard]] bool metadata_persists_in_unicode_disk_database() {
    TemporaryDatabase temporary;
    const Project project = make_project();

    {
        SqliteConnection connection{temporary.path()};
        ProjectDatabaseInitializer initializer{connection};
        initializer.initialize(project);

        if (scalar_integer(connection, "PRAGMA foreign_keys;") != 1 ||
            scalar_integer(connection, "PRAGMA busy_timeout;") != 5000 ||
            scalar_text(connection, "PRAGMA journal_mode;") != "wal") {
            return false;
        }
    }

    {
        SqliteConnection connection{temporary.path()};
        ProjectMigrationRunner migrations{connection};
        migrations.apply_pending();

        Statement statement{
            connection,
            "SELECT project_id, name, root_path, created_at_utc, updated_at_utc "
            "FROM project_metadata WHERE singleton = 1;",
        };
        if (statement.step() != SQLITE_ROW || statement.text(0) != project.id() ||
            statement.text(1) != project.name() || statement.text(2) != project.root_path() ||
            statement.text(3) != project.created_at_utc() ||
            statement.text(4) != project.updated_at_utc()) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool rejects_duplicate_project_metadata() {
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    ProjectDatabaseInitializer initializer{connection};
    initializer.initialize(make_project());

    try {
        initializer.initialize(make_project());
    } catch (const SqliteError& error) {
        if (!error.is_constraint_violation() ||
            scalar_integer(connection, "SELECT COUNT(*) FROM project_metadata;") != 1) {
            return false;
        }
        try {
            connection.execute("BEGIN IMMEDIATE; ROLLBACK;");
            return true;
        } catch (const SqliteError&) {
            return false;
        }
    }
    return false;
}

[[nodiscard]] bool rolls_back_failed_migration_and_releases_transaction() {
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    connection.execute("CREATE TABLE managed_files(unexpected_column TEXT);");

    ProjectMigrationRunner migrations{connection};
    try {
        migrations.apply_pending();
    } catch (const SqliteError&) {
        if (migrations.current_version() != 0 || table_exists(connection, "project_metadata") ||
            table_exists(connection, "jobs") || table_exists(connection, "settings")) {
            return false;
        }

        try {
            connection.execute("BEGIN IMMEDIATE; ROLLBACK;");
            return true;
        } catch (const SqliteError&) {
            return false;
        }
    }
    return false;
}


[[nodiscard]] bool rolls_back_failed_version_two_migration() {
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    connection.execute(R"sql(
        CREATE TABLE schema_migrations (
            version INTEGER PRIMARY KEY NOT NULL,
            name TEXT NOT NULL,
            applied_at_utc TEXT NOT NULL
        );
        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (1, 'create_project_core_tables', '2026-08-06T21:10:00Z');

        CREATE TABLE jobs (
            id TEXT PRIMARY KEY NOT NULL,
            status TEXT NOT NULL,
            priority TEXT NOT NULL DEFAULT 'normal',
            progress REAL NOT NULL DEFAULT 0.0,
            active_step_id TEXT,
            created_at_utc TEXT NOT NULL,
            updated_at_utc TEXT NOT NULL,
            pipeline_id TEXT
        );
    )sql");

    ProjectMigrationRunner migrations{connection};
    try {
        migrations.apply_pending();
    } catch (const SqliteError&) {
        if (migrations.current_version() != 1 ||
            scalar_integer(
                connection,
                "SELECT COUNT(*) FROM pragma_table_info('jobs') WHERE name = 'analysis_id';"
            ) != 0 ||
            scalar_integer(
                connection,
                "SELECT COUNT(*) FROM pragma_table_info('jobs') WHERE name = 'pipeline_id';"
            ) != 1) {
            return false;
        }

        try {
            connection.execute("BEGIN IMMEDIATE; ROLLBACK;");
            return true;
        } catch (const SqliteError&) {
            return false;
        }
    }
    return false;
}


[[nodiscard]] bool upgrades_version_three_with_safe_progress_backfill() {
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    connection.execute(R"sql(
        CREATE TABLE schema_migrations (
            version INTEGER PRIMARY KEY NOT NULL,
            name TEXT NOT NULL,
            applied_at_utc TEXT NOT NULL
        );
        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (3, 'register_generated_output_artifacts', '2026-08-07T10:00:00Z');

        CREATE TABLE jobs (
            id TEXT PRIMARY KEY NOT NULL,
            status TEXT NOT NULL,
            priority TEXT NOT NULL DEFAULT 'normal',
            progress REAL NOT NULL,
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
            id, status, priority, progress, created_at_utc, updated_at_utc
        ) VALUES ('job-1', 'running', 'normal', 0.42, 'created', 'updated');

        CREATE TABLE managed_files (
            id TEXT PRIMARY KEY NOT NULL,
            display_name TEXT NOT NULL DEFAULT 'legacy',
            storage_mode TEXT NOT NULL DEFAULT 'managed_copy',
            original_path TEXT,
            managed_path TEXT,
            relative_project_path TEXT,
            file_type TEXT NOT NULL DEFAULT 'unknown',
            size_bytes INTEGER NOT NULL DEFAULT 0,
            modified_at_utc TEXT,
            checksum_algorithm TEXT,
            checksum_value TEXT,
            created_at_utc TEXT NOT NULL DEFAULT 'c',
            updated_at_utc TEXT NOT NULL DEFAULT 'u'
        );
        INSERT INTO managed_files(id, storage_mode, managed_path, relative_project_path) VALUES ('artifact-1', 'generated_output', '/project/outputs/job-1--copy--result.out', 'outputs/job-1--copy--result.out');

        CREATE TABLE generated_artifacts (
            managed_file_id TEXT PRIMARY KEY NOT NULL REFERENCES managed_files(id) ON DELETE CASCADE,
            job_id TEXT NOT NULL REFERENCES jobs(id) ON DELETE CASCADE,
            step_id TEXT NOT NULL,
            output_port TEXT NOT NULL,
            plugin_id TEXT NOT NULL,
            plugin_version TEXT NOT NULL,
            module_id TEXT NOT NULL,
            file_type TEXT NOT NULL,
            relative_project_path TEXT NOT NULL UNIQUE,
            registered_at_utc TEXT NOT NULL,
            UNIQUE(job_id, step_id, output_port)
        );
        INSERT INTO generated_artifacts(
            managed_file_id, job_id, step_id, output_port, plugin_id, plugin_version,
            module_id, file_type, relative_project_path, registered_at_utc
        ) VALUES (
            'artifact-1', 'job-1', 'copy', 'result', 'org.biocore.demo', '0.1.0',
            'org.biocore.demo.copy', 'txt', 'outputs/job-1--copy--result.out',
            '2026-08-07T10:05:00Z'
        );
    )sql");

    ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(
            connection.native_handle(),
            "SELECT step_progress FROM generated_artifacts WHERE managed_file_id = 'artifact-1';",
            -1, &statement, nullptr
        ) != SQLITE_OK) {
        return false;
    }
    const int step = sqlite3_step(statement);
    const double progress = step == SQLITE_ROW ? sqlite3_column_double(statement, 0) : -1.0;
    sqlite3_finalize(statement);
    return migrations.current_version() == latest_project_schema_version && progress == 0.42;
}

[[nodiscard]] bool rejects_newer_project_schema() {
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    connection.execute(R"sql(
        CREATE TABLE schema_migrations (
            version INTEGER PRIMARY KEY NOT NULL,
            name TEXT NOT NULL,
            applied_at_utc TEXT NOT NULL
        );
    )sql");
    const auto future_version = latest_project_schema_version + 1;
    connection.execute(
        "INSERT INTO schema_migrations(version, name, applied_at_utc) VALUES (" +
        std::to_string(future_version) +
        ", 'future_project_schema', '2026-08-06T21:10:00Z');"
    );

    ProjectMigrationRunner migrations{connection};
    try {
        migrations.apply_pending();
    } catch (const SqliteError&) {
        return migrations.current_version() == future_version &&
               !table_exists(connection, "project_metadata");
    }
    return false;
}

[[nodiscard]] bool catalog_and_project_databases_are_isolated() {
    SqliteConnection catalog{std::filesystem::path{":memory:"}};
    CatalogMigrationRunner catalog_migrations{catalog};
    catalog_migrations.apply_pending();

    SqliteConnection project{std::filesystem::path{":memory:"}};
    ProjectDatabaseInitializer project_initializer{project};
    project_initializer.initialize(make_project());

    return table_exists(catalog, "catalog_projects") &&
           !table_exists(catalog, "project_metadata") &&
           !table_exists(catalog, "managed_files") &&
           table_exists(project, "project_metadata") && table_exists(project, "managed_files") &&
           !table_exists(project, "catalog_projects");
}

}  // namespace

int main() {
    if (!migration_is_idempotent_and_creates_core_schema()) {
        std::cerr << "Project database migration/schema contract failed\n";
        return EXIT_FAILURE;
    }
    if (!upgrades_version_one_jobs_without_data_loss()) {
        std::cerr << "Project database version-one upgrade contract failed\n";
        return EXIT_FAILURE;
    }
    if (!metadata_persists_in_unicode_disk_database()) {
        std::cerr << "Project database metadata persistence contract failed\n";
        return EXIT_FAILURE;
    }
    if (!rejects_duplicate_project_metadata()) {
        std::cerr << "Project database singleton metadata contract failed\n";
        return EXIT_FAILURE;
    }
    if (!rolls_back_failed_migration_and_releases_transaction()) {
        std::cerr << "Project database migration rollback contract failed\n";
        return EXIT_FAILURE;
    }
    if (!rolls_back_failed_version_two_migration()) {
        std::cerr << "Project database version-two rollback contract failed\n";
        return EXIT_FAILURE;
    }
    if (!upgrades_version_three_with_safe_progress_backfill()) {
        std::cerr << "Project database version-three checkpoint backfill contract failed\n";
        return EXIT_FAILURE;
    }
    if (!rejects_newer_project_schema()) {
        std::cerr << "Project database future-schema rejection contract failed\n";
        return EXIT_FAILURE;
    }
    if (!catalog_and_project_databases_are_isolated()) {
        std::cerr << "Catalog/project database isolation contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
