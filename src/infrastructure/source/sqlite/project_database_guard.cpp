#include "biocore/infrastructure/sqlite/project_database_guard.hpp"

#include <sqlite3.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"

namespace biocore::infrastructure::sqlite {
namespace {

struct ExpectedMigration final {
    std::int32_t version;
    std::string_view name;
};

constexpr std::array<ExpectedMigration, 7> expected_migrations{{
    {1, "create_project_core_tables"},
    {2, "extend_jobs_for_repository"},
    {3, "register_generated_output_artifacts"},
    {4, "checkpoint_generated_output_progress"},
    {5, "require_generated_output_sha256"},
    {6, "associate_prepared_job_execution_plans"},
    {7, "persist_structured_job_failure_evidence"},
}};

struct RequiredSchemaObject final {
    std::string_view type;
    std::string_view name;
};

constexpr std::array<RequiredSchemaObject, 13> required_current_objects{{
    {"table", "schema_migrations"},
    {"table", "project_metadata"},
    {"table", "managed_files"},
    {"table", "jobs"},
    {"table", "settings"},
    {"table", "generated_artifacts"},
    {"table", "job_execution_plans"},
    {"trigger", "require_generated_output_sha256_insert"},
    {"trigger", "require_generated_output_sha256_update"},
    {"trigger", "job_execution_plans_validate_insert"},
    {"trigger", "job_execution_plans_validate_update"},
    {"trigger", "jobs_validate_failure_evidence_insert"},
    {"trigger", "jobs_validate_failure_evidence_update"},
}};

class Statement final {
public:
    Statement(sqlite3* database, const char* sql, const std::string_view description)
        : database_{database}, description_{description} {
        const int result = sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr);
        if (result != SQLITE_OK) {
            throw SqliteError{
                result,
                std::string{description_} + ": " + sqlite3_errmsg(database_),
            };
        }
    }

    ~Statement() {
        if (statement_ != nullptr) {
            sqlite3_finalize(statement_);
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind_text(const int index, const std::string_view value) {
        const int result = sqlite3_bind_text(
            statement_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT
        );
        if (result != SQLITE_OK) {
            throw SqliteError{
                result,
                std::string{description_} + ": " + sqlite3_errmsg(database_),
            };
        }
    }

    [[nodiscard]] int step() {
        return sqlite3_step(statement_);
    }

    [[nodiscard]] std::int32_t integer(const int column) const {
        return static_cast<std::int32_t>(sqlite3_column_int(statement_, column));
    }

    [[nodiscard]] std::string text(const int column) const {
        const auto* raw = sqlite3_column_text(statement_, column);
        if (raw == nullptr) {
            return {};
        }
        const int bytes = sqlite3_column_bytes(statement_, column);
        return {reinterpret_cast<const char*>(raw), static_cast<std::size_t>(bytes)};
    }

private:
    sqlite3* database_;
    std::string_view description_;
    sqlite3_stmt* statement_{nullptr};
};

[[nodiscard]] bool schema_object_exists(
    sqlite3* const database,
    const std::string_view type,
    const std::string_view name
) {
    Statement statement{
        database,
        "SELECT 1 FROM sqlite_master WHERE type = ? AND name = ? LIMIT 1;",
        "Unable to inspect project schema objects",
    };
    statement.bind_text(1, type);
    statement.bind_text(2, name);
    const int result = statement.step();
    if (result == SQLITE_ROW) {
        return true;
    }
    if (result == SQLITE_DONE) {
        return false;
    }
    throw SqliteError{
        result,
        std::string{"Unable to inspect project schema objects: "} + sqlite3_errmsg(database),
    };
}

void validate_quick_check(sqlite3* const database) {
    Statement statement{database, "PRAGMA quick_check;", "Unable to run project database quick_check"};
    const int first_result = statement.step();
    if (first_result != SQLITE_ROW) {
        throw SqliteError{
            first_result,
            std::string{"Project database quick_check did not return a result: "} + sqlite3_errmsg(database),
        };
    }

    const std::string result = statement.text(0);
    if (result != "ok") {
        throw SqliteError{SQLITE_CORRUPT, "Project database quick_check failed: " + result};
    }

    const int final_result = statement.step();
    if (final_result != SQLITE_DONE) {
        throw SqliteError{
            final_result == SQLITE_ROW ? SQLITE_CORRUPT : final_result,
            final_result == SQLITE_ROW
                ? "Project database quick_check returned multiple result rows"
                : std::string{"Project database quick_check failed: "} + sqlite3_errmsg(database),
        };
    }
}

void validate_foreign_keys(sqlite3* const database) {
    Statement statement{
        database,
        "PRAGMA foreign_key_check;",
        "Unable to run project database foreign_key_check",
    };
    const int result = statement.step();
    if (result == SQLITE_DONE) {
        return;
    }
    if (result == SQLITE_ROW) {
        throw SqliteError{
            SQLITE_CONSTRAINT_FOREIGNKEY,
            "Project database foreign key violation in table '" + statement.text(0) + "'",
        };
    }
    throw SqliteError{
        result,
        std::string{"Project database foreign_key_check failed: "} + sqlite3_errmsg(database),
    };
}

void validate_migration_history(sqlite3* const database, const bool require_current) {
    const bool migration_table_exists = schema_object_exists(database, "table", "schema_migrations");
    if (!migration_table_exists) {
        if (require_current) {
            throw SqliteError{SQLITE_SCHEMA, "Project database is missing schema_migrations"};
        }
        return;
    }

    Statement statement{
        database,
        "SELECT version, name FROM schema_migrations ORDER BY version;",
        "Unable to inspect project migration history",
    };

    std::size_t expected_index = 0U;
    for (;;) {
        const int result = statement.step();
        if (result == SQLITE_DONE) {
            break;
        }
        if (result != SQLITE_ROW) {
            throw SqliteError{
                result,
                std::string{"Unable to inspect project migration history: "} + sqlite3_errmsg(database),
            };
        }
        if (expected_index >= expected_migrations.size()) {
            throw SqliteError{
                SQLITE_SCHEMA,
                "Project schema is newer than this OpenGenesis-BioCore build supports",
            };
        }

        const auto& expected = expected_migrations[expected_index];
        if (statement.integer(0) != expected.version || statement.text(1) != expected.name) {
            throw SqliteError{
                SQLITE_SCHEMA,
                "Project migration history is not the expected contiguous OpenGenesis-BioCore schema prefix",
            };
        }
        ++expected_index;
    }

    if (require_current && expected_index != expected_migrations.size()) {
        throw SqliteError{
            SQLITE_SCHEMA,
            "Project migration history does not describe the current schema version",
        };
    }
}

void validate_required_current_objects(sqlite3* const database) {
    for (const auto& object : required_current_objects) {
        if (!schema_object_exists(database, object.type, object.name)) {
            throw SqliteError{
                SQLITE_SCHEMA,
                "Project database is missing required " + std::string{object.type} + " '" +
                    std::string{object.name} + "'",
            };
        }
    }
}

}  // namespace

ProjectDatabaseGuard::ProjectDatabaseGuard(SqliteConnection& connection) noexcept
    : connection_{connection} {}

void ProjectDatabaseGuard::validate_before_migration() const {
    sqlite3* const database = connection_.native_handle();
    validate_quick_check(database);
    validate_foreign_keys(database);
    validate_migration_history(database, false);
}

void ProjectDatabaseGuard::validate_current_schema() const {
    sqlite3* const database = connection_.native_handle();
    validate_quick_check(database);
    validate_foreign_keys(database);
    validate_migration_history(database, true);
    validate_required_current_objects(database);

    ProjectMigrationRunner migrations{connection_};
    if (migrations.current_version() != latest_project_schema_version) {
        throw SqliteError{SQLITE_SCHEMA, "Project database did not reach the current schema version"};
    }
}

}  // namespace biocore::infrastructure::sqlite
