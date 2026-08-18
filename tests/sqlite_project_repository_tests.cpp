#include <sqlite3.h>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "biocore/application/i_project_repository.hpp"
#include "biocore/domain/project.hpp"
#include "biocore/infrastructure/sqlite/catalog_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"
#include "biocore/infrastructure/sqlite/sqlite_project_repository.hpp"

namespace {

using biocore::application::IProjectRepository;
using biocore::domain::Project;
using biocore::infrastructure::sqlite::CatalogMigrationRunner;
using biocore::infrastructure::sqlite::SqliteConnection;
using biocore::infrastructure::sqlite::SqliteError;
using biocore::infrastructure::sqlite::SqliteProjectRepository;

class TemporaryDatabase final {
public:
    TemporaryDatabase() {
        const auto unique_value = std::chrono::steady_clock::now().time_since_epoch().count();
        directory_ = std::filesystem::temp_directory_path() /
                     ("biocore-catalog-test-" + std::to_string(unique_value));
        std::filesystem::create_directory(directory_);
        path_ = directory_ / std::filesystem::path{u8"katalog-ü.sqlite"};
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


[[nodiscard]] std::string query_text_pragma(SqliteConnection& connection, const char* const sql) {
    sqlite3_stmt* statement = nullptr;
    sqlite3* const database = connection.native_handle();
    const int prepare_result = sqlite3_prepare_v2(database, sql, -1, &statement, nullptr);
    if (prepare_result != SQLITE_OK) {
        throw SqliteError{prepare_result, "Unable to prepare pragma query"};
    }

    const int step_result = sqlite3_step(statement);
    if (step_result != SQLITE_ROW) {
        sqlite3_finalize(statement);
        throw SqliteError{step_result, "Unable to read pragma value"};
    }

    const auto* value = sqlite3_column_text(statement, 0);
    if (value == nullptr) {
        sqlite3_finalize(statement);
        throw SqliteError{SQLITE_MISMATCH, "Pragma returned an unexpected NULL value"};
    }

    const int byte_count = sqlite3_column_bytes(statement, 0);
    const std::string result{
        reinterpret_cast<const char*>(value),
        static_cast<std::size_t>(byte_count),
    };
    const int finalize_result = sqlite3_finalize(statement);
    if (finalize_result != SQLITE_OK) {
        throw SqliteError{finalize_result, "Unable to finalize pragma query"};
    }
    return result;
}

[[nodiscard]] int query_integer_pragma(SqliteConnection& connection, const char* const sql) {
    sqlite3_stmt* statement = nullptr;
    sqlite3* const database = connection.native_handle();
    const int prepare_result = sqlite3_prepare_v2(database, sql, -1, &statement, nullptr);
    if (prepare_result != SQLITE_OK) {
        throw SqliteError{prepare_result, "Unable to prepare pragma query"};
    }

    const int step_result = sqlite3_step(statement);
    if (step_result != SQLITE_ROW) {
        sqlite3_finalize(statement);
        throw SqliteError{step_result, "Unable to read pragma value"};
    }

    const int result = sqlite3_column_int(statement, 0);
    const int finalize_result = sqlite3_finalize(statement);
    if (finalize_result != SQLITE_OK) {
        throw SqliteError{finalize_result, "Unable to finalize pragma query"};
    }
    return result;
}

[[nodiscard]] bool matches(
    const Project& project,
    const std::string_view id,
    const std::string_view name,
    const std::string_view root_path,
    const std::string_view created_at,
    const std::string_view updated_at
) {
    return project.id() == id && project.name() == name && project.root_path() == root_path &&
           project.created_at_utc() == created_at && project.updated_at_utc() == updated_at;
}

[[nodiscard]] bool run_contract(IProjectRepository& repository) {
    const Project first{
        "project-a", "Alpha", "/projects/alpha", "2026-08-06T17:00:00Z", "2026-08-06T17:00:00Z"};
    const Project second{
        "project-b", "Beta", "/projects/beta", "2026-08-06T18:00:00Z", "2026-08-06T18:00:00Z"};

    if (repository.find_by_id(first.id()).has_value() || repository.remove(first.id())) {
        std::cerr << "Empty repository contract failed\n";
        return false;
    }

    repository.save(second);
    repository.save(first);

    const auto fetched = repository.find_by_id(first.id());
    if (!fetched.has_value() ||
        !matches(*fetched, "project-a", "Alpha", "/projects/alpha", "2026-08-06T17:00:00Z", "2026-08-06T17:00:00Z")) {
        std::cerr << "Saved project could not be fetched\n";
        return false;
    }

    const auto fetched_by_root = repository.find_by_root_path(first.root_path());
    if (!fetched_by_root.has_value() || fetched_by_root->id() != first.id() ||
        repository.find_by_root_path("/projects/missing").has_value()) {
        std::cerr << "Repository root-path lookup contract failed\n";
        return false;
    }

    const Project updated{
        "project-a", "Alpha revised", "/projects/alpha-revised", "2099-01-01T00:00:00Z", "2026-08-06T19:00:00Z"};
    repository.save(updated);

    const auto updated_result = repository.find_by_id(first.id());
    if (!updated_result.has_value() ||
        !matches(
            *updated_result,
            "project-a",
            "Alpha revised",
            "/projects/alpha-revised",
            "2026-08-06T17:00:00Z",
            "2026-08-06T19:00:00Z"
        )) {
        std::cerr << "Repository update contract failed\n";
        return false;
    }

    if (repository.find_by_root_path("/projects/alpha").has_value()) {
        std::cerr << "Repository retained the previous root path after update\n";
        return false;
    }
    const auto updated_by_root = repository.find_by_root_path("/projects/alpha-revised");
    if (!updated_by_root.has_value() || updated_by_root->id() != first.id()) {
        std::cerr << "Repository root-path lookup did not follow the updated root\n";
        return false;
    }

    const auto projects = repository.list();
    if (projects.size() != 2U || projects[0].id() != "project-a" || projects[1].id() != "project-b") {
        std::cerr << "Repository list ordering contract failed\n";
        return false;
    }

    if (!repository.remove(first.id()) || repository.remove(first.id()) ||
        repository.find_by_id(first.id()).has_value()) {
        std::cerr << "Repository removal contract failed\n";
        return false;
    }

    const Project quoted_id{
        "project-'--",
        "Quoted identifier",
        "/projects/quoted-id",
        "2026-08-06T19:30:00Z",
        "2026-08-06T19:30:00Z",
    };
    repository.save(quoted_id);
    if (!repository.find_by_id(quoted_id.id()).has_value() || !repository.remove(quoted_id.id())) {
        std::cerr << "Prepared statement contract failed for quoted identifiers\n";
        return false;
    }

    return true;
}

[[nodiscard]] bool verify_persistence() {
    TemporaryDatabase temporary_database;

    {
        SqliteConnection connection{temporary_database.path()};
        if (query_integer_pragma(connection, "PRAGMA foreign_keys;") != 1 ||
            query_integer_pragma(connection, "PRAGMA busy_timeout;") != 5000 ||
            query_text_pragma(connection, "PRAGMA journal_mode;") != "wal") {
            std::cerr << "SQLite connection pragmas were not applied\n";
            return false;
        }

        CatalogMigrationRunner migrations{connection};
        migrations.apply_pending();
        SqliteProjectRepository repository{connection};
        repository.save(Project{
            "persistent-project",
            "Kalıcı genom projesi",
            "/projects/kalıcı-genom",
            "2026-08-06T20:00:00Z",
            "2026-08-06T20:00:00Z",
        });
    }

    {
        SqliteConnection connection{temporary_database.path()};
        CatalogMigrationRunner migrations{connection};
        migrations.apply_pending();
        SqliteProjectRepository repository{connection};
        const auto project = repository.find_by_id("persistent-project");
        if (!project.has_value() || project->name() != "Kalıcı genom projesi" ||
            project->root_path() != "/projects/kalıcı-genom") {
            std::cerr << "Project did not persist after reopening the SQLite database\n";
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool rolls_back_failed_migration() {
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    connection.execute("CREATE TABLE catalog_projects(unexpected_column TEXT);");

    CatalogMigrationRunner migrations{connection};
    try {
        migrations.apply_pending();
    } catch (const SqliteError&) {
        if (migrations.current_version() != 0) {
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

[[nodiscard]] bool rejects_newer_schema() {
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    connection.execute(R"sql(
        CREATE TABLE schema_migrations (
            version INTEGER PRIMARY KEY NOT NULL,
            name TEXT NOT NULL,
            applied_at_utc TEXT NOT NULL
        );
        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (2, 'future_schema', '2026-08-06T20:00:00Z');
    )sql");

    CatalogMigrationRunner migrations{connection};
    try {
        migrations.apply_pending();
    } catch (const SqliteError&) {
        return true;
    }
    return false;
}

}  // namespace

int main() {
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    CatalogMigrationRunner migrations{connection};

    migrations.apply_pending();
    migrations.apply_pending();
    if (migrations.current_version() != biocore::infrastructure::sqlite::latest_catalog_schema_version) {
        std::cerr << "Catalog migrations are not idempotent\n";
        return EXIT_FAILURE;
    }

    SqliteProjectRepository repository{connection};
    if (!run_contract(repository)) {
        return EXIT_FAILURE;
    }

    repository.save(Project{
        "project-unique-a", "Unique A", "/projects/unique", "2026-08-06T20:00:00Z", "2026-08-06T20:00:00Z"});

    try {
        repository.save(Project{
            "project-unique-b", "Unique B", "/projects/unique", "2026-08-06T20:01:00Z", "2026-08-06T20:01:00Z"});
        std::cerr << "Duplicate project roots were accepted\n";
        return EXIT_FAILURE;
    } catch (const SqliteError& error) {
        if (!error.is_constraint_violation()) {
            std::cerr << "Duplicate project root failed for an unexpected reason\n";
            return EXIT_FAILURE;
        }
    }

    if (!verify_persistence()) {
        return EXIT_FAILURE;
    }

    if (!rolls_back_failed_migration()) {
        std::cerr << "A failed migration was not rolled back\n";
        return EXIT_FAILURE;
    }

    if (!rejects_newer_schema()) {
        std::cerr << "A newer catalog schema was accepted\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
