#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "biocore/infrastructure/sqlite/project_database_guard.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"

namespace {

using biocore::infrastructure::sqlite::ProjectDatabaseGuard;
using biocore::infrastructure::sqlite::ProjectMigrationRunner;
using biocore::infrastructure::sqlite::SqliteConnection;
using biocore::infrastructure::sqlite::SqliteError;

[[nodiscard]] bool guard_throws_before_migration(ProjectDatabaseGuard& guard) {
    try {
        guard.validate_before_migration();
    } catch (const SqliteError&) {
        return true;
    }
    return false;
}

[[nodiscard]] bool guard_throws_current(ProjectDatabaseGuard& guard) {
    try {
        guard.validate_current_schema();
    } catch (const SqliteError&) {
        return true;
    }
    return false;
}

[[nodiscard]] bool accepts_clean_database_and_current_schema() {
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    ProjectDatabaseGuard guard{connection};
    guard.validate_before_migration();

    ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    guard.validate_current_schema();
    return true;
}

[[nodiscard]] bool rejects_gapped_migration_history_before_mutation() {
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    connection.execute(R"sql(
        CREATE TABLE schema_migrations (
            version INTEGER PRIMARY KEY NOT NULL,
            name TEXT NOT NULL,
            applied_at_utc TEXT NOT NULL
        );
        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (1, 'create_project_core_tables', 't');
        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (3, 'register_generated_output_artifacts', 't');
    )sql");

    ProjectDatabaseGuard guard{connection};
    if (!guard_throws_before_migration(guard)) {
        return false;
    }

    try {
        ProjectMigrationRunner migrations{connection};
        return migrations.current_version() == 3;
    } catch (const SqliteError&) {
        return false;
    }
}

[[nodiscard]] bool rejects_renamed_migration_history() {
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    connection.execute(R"sql(
        CREATE TABLE schema_migrations (
            version INTEGER PRIMARY KEY NOT NULL,
            name TEXT NOT NULL,
            applied_at_utc TEXT NOT NULL
        );
        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (1, 'unexpected_name', 't');
    )sql");

    ProjectDatabaseGuard guard{connection};
    return guard_throws_before_migration(guard);
}

[[nodiscard]] bool rejects_missing_required_current_schema_object() {
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    connection.execute("DROP TRIGGER job_execution_plans_validate_update;");

    ProjectDatabaseGuard guard{connection};
    return guard_throws_current(guard);
}

[[nodiscard]] bool rejects_foreign_key_violation() {
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();

    connection.execute("PRAGMA foreign_keys = OFF;");
    connection.execute(R"sql(
        INSERT INTO generated_artifacts(
            managed_file_id,
            job_id,
            step_id,
            output_port,
            plugin_id,
            plugin_version,
            module_id,
            file_type,
            relative_project_path,
            registered_at_utc,
            step_progress
        ) VALUES (
            'missing-file',
            'missing-job',
            'step',
            'result',
            'org.biocore.test',
            '0.1.0',
            'org.biocore.test.module',
            'txt',
            'outputs/orphan.txt',
            't',
            0.0
        );
    )sql");
    connection.execute("PRAGMA foreign_keys = ON;");

    ProjectDatabaseGuard guard{connection};
    return guard_throws_current(guard);
}

[[nodiscard]] bool rejects_future_migration_history() {
    SqliteConnection connection{std::filesystem::path{":memory:"}};
    connection.execute(R"sql(
        CREATE TABLE schema_migrations (
            version INTEGER PRIMARY KEY NOT NULL,
            name TEXT NOT NULL,
            applied_at_utc TEXT NOT NULL
        );
        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (1, 'create_project_core_tables', 't');
        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (2, 'extend_jobs_for_repository', 't');
        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (3, 'register_generated_output_artifacts', 't');
        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (4, 'checkpoint_generated_output_progress', 't');
        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (5, 'require_generated_output_sha256', 't');
        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (6, 'associate_prepared_job_execution_plans', 't');
        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (7, 'future_schema', 't');
    )sql");

    ProjectDatabaseGuard guard{connection};
    return guard_throws_before_migration(guard);
}

}  // namespace

int main() {
    if (!accepts_clean_database_and_current_schema()) {
        std::cerr << "Project database guard clean-schema contract failed\n";
        return EXIT_FAILURE;
    }
    if (!rejects_gapped_migration_history_before_mutation()) {
        std::cerr << "Project database guard migration-gap contract failed\n";
        return EXIT_FAILURE;
    }
    if (!rejects_renamed_migration_history()) {
        std::cerr << "Project database guard migration-name contract failed\n";
        return EXIT_FAILURE;
    }
    if (!rejects_missing_required_current_schema_object()) {
        std::cerr << "Project database guard required-object contract failed\n";
        return EXIT_FAILURE;
    }
    if (!rejects_foreign_key_violation()) {
        std::cerr << "Project database guard foreign-key contract failed\n";
        return EXIT_FAILURE;
    }
    if (!rejects_future_migration_history()) {
        std::cerr << "Project database guard future-schema contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
