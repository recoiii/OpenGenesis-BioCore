#include "biocore/infrastructure/sqlite/catalog_migration_runner.hpp"

#include <sqlite3.h>

#include <cstdint>
#include <string>

#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"

namespace biocore::infrastructure::sqlite {
namespace {

class Transaction final {
public:
    explicit Transaction(SqliteConnection& connection) : connection_{connection} {
        connection_.execute("BEGIN IMMEDIATE;");
    }

    ~Transaction() {
        if (!committed_) {
            try {
                connection_.execute("ROLLBACK;");
            } catch (...) {
                // Destructors must not emit exceptions. The original failure remains authoritative.
            }
        }
    }

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit() {
        connection_.execute("COMMIT;");
        committed_ = true;
    }

private:
    SqliteConnection& connection_;
    bool committed_{false};
};

[[nodiscard]] std::int32_t read_current_version(sqlite3* const database) {
    constexpr const char* sql = "SELECT COALESCE(MAX(version), 0) FROM schema_migrations;";
    sqlite3_stmt* statement = nullptr;
    const int prepare_result = sqlite3_prepare_v2(database, sql, -1, &statement, nullptr);
    if (prepare_result != SQLITE_OK) {
        throw SqliteError{
            prepare_result,
            std::string{"Unable to read catalog schema version: "} + sqlite3_errmsg(database),
        };
    }

    const int step_result = sqlite3_step(statement);
    if (step_result != SQLITE_ROW) {
        const std::string message = std::string{"Unable to read catalog schema version: "} + sqlite3_errmsg(database);
        sqlite3_finalize(statement);
        throw SqliteError{step_result, message};
    }

    const auto version = static_cast<std::int32_t>(sqlite3_column_int(statement, 0));
    const int finalize_result = sqlite3_finalize(statement);
    if (finalize_result != SQLITE_OK) {
        throw SqliteError{
            finalize_result,
            std::string{"Unable to finalize schema version query: "} + sqlite3_errmsg(database),
        };
    }
    return version;
}

void apply_version_one(SqliteConnection& connection) {
    connection.execute(R"sql(
        CREATE TABLE catalog_projects (
            id TEXT PRIMARY KEY NOT NULL CHECK(length(id) BETWEEN 1 AND 128),
            name TEXT NOT NULL CHECK(length(trim(name)) BETWEEN 1 AND 200),
            root_path TEXT NOT NULL CHECK(length(root_path) > 0),
            created_at_utc TEXT NOT NULL CHECK(length(created_at_utc) > 0),
            updated_at_utc TEXT NOT NULL CHECK(length(updated_at_utc) > 0)
        );

        CREATE UNIQUE INDEX idx_catalog_projects_root_path
            ON catalog_projects(root_path);

        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (1, 'create_catalog_projects', strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
    )sql");
}

}  // namespace

CatalogMigrationRunner::CatalogMigrationRunner(SqliteConnection& connection) noexcept : connection_{connection} {}

void CatalogMigrationRunner::apply_pending() {
    connection_.execute(R"sql(
        CREATE TABLE IF NOT EXISTS schema_migrations (
            version INTEGER PRIMARY KEY NOT NULL,
            name TEXT NOT NULL,
            applied_at_utc TEXT NOT NULL
        );
    )sql");

    const std::int32_t version = current_version();
    if (version > latest_catalog_schema_version) {
        throw SqliteError{SQLITE_ERROR, "Catalog schema is newer than this OpenGenesis-BioCore build supports"};
    }

    if (version == latest_catalog_schema_version) {
        return;
    }

    Transaction transaction{connection_};
    if (version < 1) {
        apply_version_one(connection_);
    }
    transaction.commit();
}

std::int32_t CatalogMigrationRunner::current_version() const {
    return read_current_version(connection_.native_handle());
}

}  // namespace biocore::infrastructure::sqlite
