#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"

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
            std::string{"Unable to read project schema version: "} + sqlite3_errmsg(database),
        };
    }

    const int step_result = sqlite3_step(statement);
    if (step_result != SQLITE_ROW) {
        const std::string message =
            std::string{"Unable to read project schema version: "} + sqlite3_errmsg(database);
        sqlite3_finalize(statement);
        throw SqliteError{step_result, message};
    }

    const auto version = static_cast<std::int32_t>(sqlite3_column_int(statement, 0));
    const int finalize_result = sqlite3_finalize(statement);
    if (finalize_result != SQLITE_OK) {
        throw SqliteError{
            finalize_result,
            std::string{"Unable to finalize project schema version query: "} +
                sqlite3_errmsg(database),
        };
    }
    return version;
}

void apply_version_one(SqliteConnection& connection) {
    connection.execute(R"sql(
        CREATE TABLE project_metadata (
            singleton INTEGER PRIMARY KEY NOT NULL CHECK(singleton = 1),
            project_id TEXT UNIQUE NOT NULL CHECK(length(project_id) BETWEEN 1 AND 128),
            name TEXT NOT NULL CHECK(length(trim(name)) BETWEEN 1 AND 200),
            root_path TEXT UNIQUE NOT NULL CHECK(length(root_path) > 0),
            created_at_utc TEXT NOT NULL CHECK(length(created_at_utc) > 0),
            updated_at_utc TEXT NOT NULL CHECK(length(updated_at_utc) > 0)
        );

        CREATE TABLE managed_files (
            id TEXT PRIMARY KEY NOT NULL CHECK(length(id) BETWEEN 1 AND 128),
            display_name TEXT NOT NULL CHECK(length(trim(display_name)) BETWEEN 1 AND 255),
            storage_mode TEXT NOT NULL CHECK(storage_mode IN (
                'managed_copy',
                'external_reference',
                'managed_move',
                'generated_output',
                'temporary'
            )),
            original_path TEXT,
            managed_path TEXT,
            relative_project_path TEXT,
            file_type TEXT NOT NULL CHECK(length(file_type) > 0),
            size_bytes INTEGER NOT NULL CHECK(size_bytes >= 0),
            modified_at_utc TEXT,
            checksum_algorithm TEXT,
            checksum_value TEXT,
            created_at_utc TEXT NOT NULL CHECK(length(created_at_utc) > 0),
            updated_at_utc TEXT NOT NULL CHECK(length(updated_at_utc) > 0),
            CHECK(
                original_path IS NOT NULL OR
                managed_path IS NOT NULL OR
                relative_project_path IS NOT NULL
            )
        );

        CREATE UNIQUE INDEX idx_managed_files_relative_project_path
            ON managed_files(relative_project_path)
            WHERE relative_project_path IS NOT NULL;

        CREATE TABLE jobs (
            id TEXT PRIMARY KEY NOT NULL CHECK(length(id) BETWEEN 1 AND 128),
            status TEXT NOT NULL CHECK(status IN (
                'draft',
                'queued',
                'preparing',
                'running',
                'paused',
                'cancelling',
                'cancelled',
                'completed',
                'failed',
                'interrupted'
            )),
            priority TEXT NOT NULL DEFAULT 'normal' CHECK(priority IN ('low', 'normal', 'high')),
            progress REAL NOT NULL DEFAULT 0.0 CHECK(progress >= 0.0 AND progress <= 1.0),
            active_step_id TEXT,
            created_at_utc TEXT NOT NULL CHECK(length(created_at_utc) > 0),
            updated_at_utc TEXT NOT NULL CHECK(length(updated_at_utc) > 0)
        );

        CREATE INDEX idx_jobs_status_created_at
            ON jobs(status, created_at_utc);

        CREATE TABLE settings (
            key TEXT PRIMARY KEY NOT NULL CHECK(length(trim(key)) BETWEEN 1 AND 200),
            value_json TEXT NOT NULL CHECK(length(value_json) > 0),
            updated_at_utc TEXT NOT NULL CHECK(length(updated_at_utc) > 0)
        );

        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (1, 'create_project_core_tables', strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
    )sql");
}

void apply_version_two(SqliteConnection& connection) {
    connection.execute(R"sql(
        ALTER TABLE jobs ADD COLUMN analysis_id TEXT
            CHECK(analysis_id IS NULL OR length(trim(analysis_id)) BETWEEN 1 AND 128);
        ALTER TABLE jobs ADD COLUMN pipeline_id TEXT
            CHECK(pipeline_id IS NULL OR length(trim(pipeline_id)) BETWEEN 1 AND 200);
        ALTER TABLE jobs ADD COLUMN pipeline_version TEXT
            CHECK(pipeline_version IS NULL OR length(trim(pipeline_version)) BETWEEN 1 AND 200);
        ALTER TABLE jobs ADD COLUMN started_at_utc TEXT
            CHECK(started_at_utc IS NULL OR length(started_at_utc) > 0);
        ALTER TABLE jobs ADD COLUMN finished_at_utc TEXT
            CHECK(finished_at_utc IS NULL OR length(finished_at_utc) > 0);
        ALTER TABLE jobs ADD COLUMN revision INTEGER NOT NULL DEFAULT 0 CHECK(revision >= 0);

        UPDATE jobs
        SET started_at_utc = updated_at_utc
        WHERE status IN (
            'preparing', 'running', 'paused', 'cancelling',
            'completed', 'failed', 'interrupted'
        ) AND started_at_utc IS NULL;

        UPDATE jobs
        SET finished_at_utc = updated_at_utc,
            active_step_id = NULL
        WHERE status IN ('cancelled', 'completed', 'failed');

        UPDATE jobs SET progress = 1.0 WHERE status = 'completed';

        CREATE INDEX idx_jobs_created_at_id ON jobs(created_at_utc, id);

        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (2, 'extend_jobs_for_repository', strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
    )sql");
}

void apply_version_three(SqliteConnection& connection) {
    connection.execute(R"sql(
        CREATE TABLE generated_artifacts (
            managed_file_id TEXT PRIMARY KEY NOT NULL
                REFERENCES managed_files(id) ON DELETE CASCADE,
            job_id TEXT NOT NULL REFERENCES jobs(id) ON DELETE CASCADE
                CHECK(length(job_id) BETWEEN 1 AND 128),
            step_id TEXT NOT NULL CHECK(length(trim(step_id)) BETWEEN 1 AND 200),
            output_port TEXT NOT NULL CHECK(length(trim(output_port)) BETWEEN 1 AND 200),
            plugin_id TEXT NOT NULL CHECK(length(trim(plugin_id)) BETWEEN 1 AND 200),
            plugin_version TEXT NOT NULL CHECK(length(trim(plugin_version)) BETWEEN 1 AND 200),
            module_id TEXT NOT NULL CHECK(length(trim(module_id)) BETWEEN 1 AND 200),
            file_type TEXT NOT NULL CHECK(length(trim(file_type)) BETWEEN 1 AND 128),
            relative_project_path TEXT NOT NULL UNIQUE CHECK(length(relative_project_path) > 0),
            registered_at_utc TEXT NOT NULL CHECK(length(registered_at_utc) > 0),
            UNIQUE(job_id, step_id, output_port)
        );

        CREATE INDEX idx_generated_artifacts_job_step
            ON generated_artifacts(job_id, step_id, output_port);

        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (3, 'register_generated_output_artifacts', strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
    )sql");
}

void apply_version_four(SqliteConnection& connection) {
    connection.execute(R"sql(
        ALTER TABLE generated_artifacts ADD COLUMN step_progress REAL NOT NULL DEFAULT 0.0
            CHECK(step_progress >= 0.0 AND step_progress <= 1.0);

        UPDATE generated_artifacts
        SET step_progress = COALESCE((
            SELECT jobs.progress FROM jobs WHERE jobs.id = generated_artifacts.job_id
        ), 0.0);

        CREATE INDEX idx_generated_artifacts_job_progress
            ON generated_artifacts(job_id, step_progress);

        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (4, 'checkpoint_generated_output_progress', strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
    )sql");
}

void apply_version_five(SqliteConnection& connection) {
    connection.execute(R"sql(
        CREATE TRIGGER require_generated_output_sha256_insert
        BEFORE INSERT ON managed_files
        WHEN NEW.storage_mode = 'generated_output' AND (
            NEW.checksum_algorithm IS NULL OR
            NEW.checksum_value IS NULL OR
            NEW.checksum_algorithm != 'sha256' OR
            length(NEW.checksum_value) != 64 OR
            NEW.checksum_value GLOB '*[^0-9a-f]*'
        )
        BEGIN
            SELECT RAISE(ABORT, 'generated outputs require a lowercase SHA-256 checksum');
        END;

        CREATE TRIGGER require_generated_output_sha256_update
        BEFORE UPDATE OF storage_mode, checksum_algorithm, checksum_value ON managed_files
        WHEN NEW.storage_mode = 'generated_output' AND (
            NEW.checksum_algorithm IS NULL OR
            NEW.checksum_value IS NULL OR
            NEW.checksum_algorithm != 'sha256' OR
            length(NEW.checksum_value) != 64 OR
            NEW.checksum_value GLOB '*[^0-9a-f]*'
        )
        BEGIN
            SELECT RAISE(ABORT, 'generated outputs require a lowercase SHA-256 checksum');
        END;

        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (5, 'require_generated_output_sha256', strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
    )sql");
}

void apply_version_six(SqliteConnection& connection) {
    connection.execute(R"sql(
        CREATE TABLE job_execution_plans (
            job_id TEXT PRIMARY KEY NOT NULL REFERENCES jobs(id) ON DELETE CASCADE,
            launch_revision INTEGER NOT NULL CHECK(launch_revision >= 1),
            pipeline_id TEXT NOT NULL CHECK(length(trim(pipeline_id)) BETWEEN 1 AND 200),
            pipeline_version TEXT NOT NULL CHECK(length(trim(pipeline_version)) BETWEEN 1 AND 200),
            execution_plan_path TEXT NOT NULL UNIQUE CHECK(length(execution_plan_path) > 0),
            prepared_at_utc TEXT NOT NULL CHECK(length(prepared_at_utc) > 0),
            CHECK(instr(execution_plan_path, char(0)) = 0)
        );

        CREATE INDEX idx_job_execution_plans_pipeline
            ON job_execution_plans(pipeline_id, pipeline_version);

        CREATE TRIGGER job_execution_plans_validate_insert
        BEFORE INSERT ON job_execution_plans
        WHEN NOT EXISTS (
            SELECT 1 FROM jobs
            WHERE id = NEW.job_id
              AND status = 'queued'
              AND revision + 1 = NEW.launch_revision
              AND pipeline_id = NEW.pipeline_id
              AND pipeline_version = NEW.pipeline_version
        )
        BEGIN
            SELECT RAISE(ABORT, 'prepared execution plan must match a queued job revision and pipeline');
        END;

        CREATE TRIGGER job_execution_plans_validate_update
        BEFORE UPDATE ON job_execution_plans
        WHEN NOT EXISTS (
            SELECT 1 FROM jobs
            WHERE id = NEW.job_id
              AND status = 'queued'
              AND revision + 1 = NEW.launch_revision
              AND pipeline_id = NEW.pipeline_id
              AND pipeline_version = NEW.pipeline_version
        )
        BEGIN
            SELECT RAISE(ABORT, 'prepared execution plan must match a queued job revision and pipeline');
        END;

        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (6, 'associate_prepared_job_execution_plans', strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
    )sql");
}

}  // namespace

ProjectMigrationRunner::ProjectMigrationRunner(SqliteConnection& connection) noexcept
    : connection_{connection} {}

void ProjectMigrationRunner::apply_pending() {
    connection_.execute(R"sql(
        CREATE TABLE IF NOT EXISTS schema_migrations (
            version INTEGER PRIMARY KEY NOT NULL,
            name TEXT NOT NULL,
            applied_at_utc TEXT NOT NULL
        );
    )sql");

    const std::int32_t version = current_version();
    if (version > latest_project_schema_version) {
        throw SqliteError{SQLITE_ERROR, "Project schema is newer than this OpenGenesis-BioCore build supports"};
    }

    if (version == latest_project_schema_version) {
        return;
    }

    Transaction transaction{connection_};
    if (version < 1) {
        apply_version_one(connection_);
    }
    if (version < 2) {
        apply_version_two(connection_);
    }
    if (version < 3) {
        apply_version_three(connection_);
    }
    if (version < 4) {
        apply_version_four(connection_);
    }
    if (version < 5) {
        apply_version_five(connection_);
    }
    if (version < 6) {
        apply_version_six(connection_);
    }
    transaction.commit();
}

std::int32_t ProjectMigrationRunner::current_version() const {
    return read_current_version(connection_.native_handle());
}

}  // namespace biocore::infrastructure::sqlite
