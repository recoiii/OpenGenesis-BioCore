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

void apply_version_seven(SqliteConnection& connection) {
    connection.execute(R"sql(
        ALTER TABLE jobs ADD COLUMN failure_kind TEXT
            CHECK(failure_kind IS NULL OR failure_kind IN (
                'worker_reported_failure',
                'process_exit_without_terminal',
                'heartbeat_timeout',
                'startup_recovery',
                'unspecified_terminal_failure',
                'legacy_terminal_state'
            ));
        ALTER TABLE jobs ADD COLUMN failure_message TEXT
            CHECK(failure_message IS NULL OR (
                length(trim(failure_message)) >= 1 AND
                length(CAST(failure_message AS BLOB)) <= 16384 AND
                instr(failure_message, char(0)) = 0
            ));
        ALTER TABLE jobs ADD COLUMN failure_exit_code INTEGER
            CHECK(failure_exit_code IS NULL OR failure_exit_code >= 0);
        ALTER TABLE jobs ADD COLUMN failure_worker_timestamp_utc TEXT
            CHECK(failure_worker_timestamp_utc IS NULL OR (
                length(trim(failure_worker_timestamp_utc)) >= 1 AND
                length(CAST(failure_worker_timestamp_utc AS BLOB)) <= 200 AND
                instr(failure_worker_timestamp_utc, char(0)) = 0
            ));
        ALTER TABLE jobs ADD COLUMN failure_recorded_at_utc TEXT
            CHECK(failure_recorded_at_utc IS NULL OR (
                length(trim(failure_recorded_at_utc)) >= 1 AND
                length(CAST(failure_recorded_at_utc AS BLOB)) <= 200 AND
                instr(failure_recorded_at_utc, char(0)) = 0
            ));

        UPDATE jobs
        SET failure_kind = 'legacy_terminal_state',
            failure_message = 'Terminal state predates structured failure evidence.',
            failure_recorded_at_utc = COALESCE(finished_at_utc, updated_at_utc)
        WHERE status IN ('failed', 'interrupted');

        CREATE TRIGGER jobs_validate_failure_evidence_insert
        BEFORE INSERT ON jobs
        WHEN (
            (NEW.status IN ('failed', 'interrupted') AND (
                NEW.failure_kind IS NULL OR NEW.failure_message IS NULL OR
                NEW.failure_recorded_at_utc IS NULL
            )) OR
            (NEW.status NOT IN ('failed', 'interrupted') AND (
                NEW.failure_kind IS NOT NULL OR NEW.failure_message IS NOT NULL OR
                NEW.failure_exit_code IS NOT NULL OR
                NEW.failure_worker_timestamp_utc IS NOT NULL OR
                NEW.failure_recorded_at_utc IS NOT NULL
            )) OR
            (NEW.failure_kind = 'worker_reported_failure' AND (
                NEW.status != 'failed' OR NEW.failure_exit_code IS NULL OR
                NEW.failure_exit_code = 0 OR NEW.failure_worker_timestamp_utc IS NULL
            )) OR
            (NEW.failure_kind = 'process_exit_without_terminal' AND (
                NEW.status != 'interrupted' OR NEW.failure_exit_code IS NULL OR
                NEW.failure_worker_timestamp_utc IS NOT NULL
            )) OR
            (NEW.failure_kind IN ('heartbeat_timeout', 'startup_recovery') AND
                NEW.status != 'interrupted') OR
            (NEW.failure_kind NOT IN ('worker_reported_failure', 'process_exit_without_terminal') AND
                NEW.failure_exit_code IS NOT NULL) OR
            (NEW.failure_kind != 'worker_reported_failure' AND
                NEW.failure_worker_timestamp_utc IS NOT NULL)
        )
        BEGIN
            SELECT RAISE(ABORT, 'job failure evidence does not match terminal job state');
        END;

        CREATE TRIGGER jobs_validate_failure_evidence_update
        BEFORE UPDATE ON jobs
        WHEN (
            (NEW.status IN ('failed', 'interrupted') AND (
                NEW.failure_kind IS NULL OR NEW.failure_message IS NULL OR
                NEW.failure_recorded_at_utc IS NULL
            )) OR
            (NEW.status NOT IN ('failed', 'interrupted') AND (
                NEW.failure_kind IS NOT NULL OR NEW.failure_message IS NOT NULL OR
                NEW.failure_exit_code IS NOT NULL OR
                NEW.failure_worker_timestamp_utc IS NOT NULL OR
                NEW.failure_recorded_at_utc IS NOT NULL
            )) OR
            (NEW.failure_kind = 'worker_reported_failure' AND (
                NEW.status != 'failed' OR NEW.failure_exit_code IS NULL OR
                NEW.failure_exit_code = 0 OR NEW.failure_worker_timestamp_utc IS NULL
            )) OR
            (NEW.failure_kind = 'process_exit_without_terminal' AND (
                NEW.status != 'interrupted' OR NEW.failure_exit_code IS NULL OR
                NEW.failure_worker_timestamp_utc IS NOT NULL
            )) OR
            (NEW.failure_kind IN ('heartbeat_timeout', 'startup_recovery') AND
                NEW.status != 'interrupted') OR
            (NEW.failure_kind NOT IN ('worker_reported_failure', 'process_exit_without_terminal') AND
                NEW.failure_exit_code IS NOT NULL) OR
            (NEW.failure_kind != 'worker_reported_failure' AND
                NEW.failure_worker_timestamp_utc IS NOT NULL)
        )
        BEGIN
            SELECT RAISE(ABORT, 'job failure evidence does not match terminal job state');
        END;

        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (7, 'persist_structured_job_failure_evidence', strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
    )sql");
 }

void apply_version_eight(SqliteConnection& connection) {
    connection.execute(R"sql(
        ALTER TABLE jobs ADD COLUMN attempt_number INTEGER NOT NULL DEFAULT 1
            CHECK(attempt_number >= 1);

        CREATE TRIGGER jobs_validate_attempt_update
        BEFORE UPDATE ON jobs
        WHEN (
            (OLD.status = 'interrupted' AND NEW.status = 'queued' AND (
                NEW.attempt_number != OLD.attempt_number + 1 OR
                NEW.progress != 0.0 OR
                NEW.active_step_id IS NOT NULL OR
                NEW.started_at_utc IS NOT NULL OR
                NEW.finished_at_utc IS NOT NULL OR
                NEW.failure_kind IS NOT NULL OR NEW.failure_message IS NOT NULL OR
                NEW.failure_exit_code IS NOT NULL OR
                NEW.failure_worker_timestamp_utc IS NOT NULL OR
                NEW.failure_recorded_at_utc IS NOT NULL
            )) OR
            (NOT (OLD.status = 'interrupted' AND NEW.status = 'queued') AND
                NEW.attempt_number != OLD.attempt_number)
        )
        BEGIN
            SELECT RAISE(ABORT, 'job attempt number may only advance on a clean interrupted retry');
        END;

        CREATE TRIGGER job_execution_plans_immutable_fields_update
        BEFORE UPDATE OF pipeline_id, pipeline_version, execution_plan_path, prepared_at_utc
        ON job_execution_plans
        WHEN NEW.pipeline_id != OLD.pipeline_id OR
             NEW.pipeline_version != OLD.pipeline_version OR
             NEW.execution_plan_path != OLD.execution_plan_path OR
             NEW.prepared_at_utc != OLD.prepared_at_utc
        BEGIN
            SELECT RAISE(ABORT, 'prepared execution plan identity is immutable across retries');
        END;

        CREATE TRIGGER job_execution_plans_launch_revision_monotonic
        BEFORE UPDATE OF launch_revision ON job_execution_plans
        WHEN NEW.launch_revision <= OLD.launch_revision
        BEGIN
            SELECT RAISE(ABORT, 'prepared execution launch revision must advance monotonically');
        END;

        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (8, 'add_explicit_retry_attempt_semantics', strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
    )sql");
}

}  // namespace

void apply_version_nine(SqliteConnection& connection) {
    connection.execute(R"sql(
        CREATE TABLE workflow_states (
            workflow_id TEXT PRIMARY KEY NOT NULL
                CHECK(length(workflow_id) BETWEEN 1 AND 256),
            workflow_document_json TEXT NOT NULL
                CHECK(length(workflow_document_json) > 0 AND
                      instr(workflow_document_json, char(0)) = 0),
            checkpoint_schema_version INTEGER NOT NULL
                CHECK(checkpoint_schema_version = 1),
            branch_decision_schema_version INTEGER
                CHECK(branch_decision_schema_version IS NULL OR
                      branch_decision_schema_version = 1),
            revision INTEGER NOT NULL DEFAULT 0 CHECK(revision >= 0),
            updated_at_utc TEXT NOT NULL
                CHECK(length(trim(updated_at_utc)) > 0 AND
                      instr(updated_at_utc, char(0)) = 0)
        );

        CREATE TABLE workflow_node_checkpoints (
            workflow_id TEXT NOT NULL
                REFERENCES workflow_states(workflow_id) ON DELETE CASCADE,
            node_id TEXT NOT NULL CHECK(length(node_id) BETWEEN 1 AND 128),
            ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
            state TEXT NOT NULL CHECK(state IN (
                'pending', 'running', 'completed', 'failed',
                'interrupted', 'skipped', 'blocked'
            )),
            attempt_number INTEGER NOT NULL CHECK(attempt_number >= 0),
            max_attempts INTEGER NOT NULL CHECK(max_attempts BETWEEN 1 AND 64),
            failure_message TEXT,
            failure_exit_code INTEGER,
            PRIMARY KEY(workflow_id, node_id),
            UNIQUE(workflow_id, ordinal),
            CHECK(attempt_number <= max_attempts),
            CHECK(
                (state IN ('pending', 'skipped', 'blocked') AND attempt_number = 0) OR
                (state NOT IN ('pending', 'skipped', 'blocked') AND attempt_number >= 1)
            ),
            CHECK(
                (state IN ('failed', 'interrupted') AND
                    failure_message IS NOT NULL AND
                    length(trim(failure_message)) >= 1 AND
                    length(CAST(failure_message AS BLOB)) <= 16384 AND
                    instr(failure_message, char(0)) = 0) OR
                (state NOT IN ('failed', 'interrupted') AND
                    failure_message IS NULL AND failure_exit_code IS NULL)
            )
        );

        CREATE TABLE workflow_checkpoint_artifacts (
            workflow_id TEXT NOT NULL,
            node_id TEXT NOT NULL,
            output_port TEXT NOT NULL
                CHECK(length(output_port) BETWEEN 1 AND 64),
            ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
            relative_project_path TEXT NOT NULL
                CHECK(length(relative_project_path) BETWEEN 1 AND 4096 AND
                      instr(relative_project_path, char(0)) = 0),
            size_bytes INTEGER NOT NULL CHECK(size_bytes >= 0),
            sha256 TEXT NOT NULL CHECK(
                length(sha256) = 64 AND sha256 NOT GLOB '*[^0-9a-f]*'
            ),
            PRIMARY KEY(workflow_id, node_id, output_port),
            UNIQUE(workflow_id, node_id, ordinal),
            FOREIGN KEY(workflow_id, node_id)
                REFERENCES workflow_node_checkpoints(workflow_id, node_id)
                ON DELETE CASCADE
        );

        CREATE TABLE workflow_branch_decisions (
            workflow_id TEXT NOT NULL
                REFERENCES workflow_states(workflow_id) ON DELETE CASCADE,
            node_id TEXT NOT NULL CHECK(length(node_id) BETWEEN 1 AND 128),
            ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
            state TEXT NOT NULL CHECK(state IN (
                'selected', 'skipped', 'blocked', 'deferred'
            )),
            reason TEXT NOT NULL CHECK(reason IN (
                'unconditional', 'condition_true', 'condition_false',
                'condition_unresolved', 'required_input_unavailable',
                'condition_source_unavailable'
            )),
            condition_result INTEGER CHECK(
                condition_result IS NULL OR condition_result IN (0, 1)
            ),
            PRIMARY KEY(workflow_id, node_id),
            UNIQUE(workflow_id, ordinal),
            CHECK(
                (state = 'selected' AND reason = 'unconditional' AND
                    condition_result IS NULL) OR
                (state = 'selected' AND reason = 'condition_true' AND
                    condition_result = 1) OR
                (state = 'skipped' AND reason = 'condition_false' AND
                    condition_result = 0) OR
                (state = 'deferred' AND reason = 'condition_unresolved' AND
                    condition_result IS NULL) OR
                (state = 'blocked' AND
                    reason IN ('required_input_unavailable',
                               'condition_source_unavailable') AND
                    condition_result IS NULL)
            )
        );

        CREATE TRIGGER workflow_branch_decisions_require_snapshot_insert
        BEFORE INSERT ON workflow_branch_decisions
        WHEN NOT EXISTS (
            SELECT 1 FROM workflow_states
            WHERE workflow_id = NEW.workflow_id
              AND branch_decision_schema_version = 1
        )
        BEGIN
            SELECT RAISE(
                ABORT,
                'workflow branch decisions require an active decision snapshot'
            );
        END;

        CREATE TRIGGER workflow_states_prevent_branch_schema_clear
        BEFORE UPDATE OF branch_decision_schema_version ON workflow_states
        WHEN NEW.branch_decision_schema_version IS NULL AND EXISTS (
            SELECT 1 FROM workflow_branch_decisions
            WHERE workflow_id = NEW.workflow_id
        )
        BEGIN
            SELECT RAISE(
                ABORT,
                'workflow branch decision rows must be removed before clearing schema'
            );
        END;

        CREATE INDEX idx_workflow_states_revision
            ON workflow_states(workflow_id, revision);

        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (
            9,
            'persist_workflow_state_and_recovery',
            strftime('%Y-%m-%dT%H:%M:%fZ', 'now')
        );
    )sql");
}

void apply_version_ten(SqliteConnection& connection) {
    connection.execute(R"sql(
        ALTER TABLE project_metadata ADD COLUMN research_description TEXT NOT NULL DEFAULT ''
            CHECK(length(CAST(research_description AS BLOB)) <= 4096 AND
                  instr(research_description, char(0)) = 0);
        ALTER TABLE project_metadata ADD COLUMN research_organism TEXT NOT NULL DEFAULT ''
            CHECK(length(CAST(research_organism AS BLOB)) <= 256 AND
                  instr(research_organism, char(0)) = 0);
        ALTER TABLE project_metadata ADD COLUMN research_revision INTEGER NOT NULL DEFAULT 0
            CHECK(typeof(research_revision) = 'integer' AND research_revision >= 0);
        ALTER TABLE project_metadata ADD COLUMN research_updated_at_utc TEXT NOT NULL DEFAULT ''
            CHECK(instr(research_updated_at_utc, char(0)) = 0 AND
                  (research_revision = 0 OR length(trim(research_updated_at_utc, char(9)||char(10)||char(11)||char(12)||char(13)||' ')) > 0));
        UPDATE project_metadata SET research_updated_at_utc = updated_at_utc;

        CREATE TRIGGER project_research_metadata_initialize
        AFTER INSERT ON project_metadata
        WHEN NEW.research_updated_at_utc = ''
        BEGIN
            UPDATE project_metadata SET research_updated_at_utc = NEW.updated_at_utc
            WHERE singleton = NEW.singleton;
        END;

        CREATE TRIGGER project_research_metadata_revision
        BEFORE UPDATE OF research_description, research_organism, research_revision, research_updated_at_utc
        ON project_metadata
        WHEN NOT (
            OLD.research_updated_at_utc = '' AND
            NEW.research_updated_at_utc = OLD.updated_at_utc AND
            NEW.research_revision = OLD.research_revision AND
            NEW.research_description = OLD.research_description AND
            NEW.research_organism = OLD.research_organism
        ) AND NEW.research_revision != OLD.research_revision + 1
        BEGIN
            SELECT RAISE(ABORT, 'project research metadata revision must advance by one');
        END;

        CREATE TRIGGER project_metadata_identity_immutable
        BEFORE UPDATE OF project_id ON project_metadata
        WHEN NEW.project_id != OLD.project_id
        BEGIN
            SELECT RAISE(ABORT, 'project identity is immutable');
        END;

        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (10, 'add_project_research_metadata', strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
    )sql");
}


void apply_version_eleven(SqliteConnection& connection) {
    connection.execute(R"sql(
        CREATE TABLE project_samples (
            project_id TEXT NOT NULL
                REFERENCES project_metadata(project_id) ON DELETE CASCADE,
            sample_id TEXT NOT NULL CHECK(
                length(CAST(sample_id AS BLOB)) BETWEEN 1 AND 128 AND
                instr(sample_id, char(0)) = 0 AND
                length(trim(sample_id, char(9)||char(10)||char(11)||char(12)||char(13)||' ')) > 0
            ),
            display_name TEXT NOT NULL DEFAULT '' CHECK(
                length(CAST(display_name AS BLOB)) <= 255 AND
                instr(display_name, char(0)) = 0
            ),
            group_label TEXT NOT NULL DEFAULT '' CHECK(
                length(CAST(group_label AS BLOB)) <= 128 AND
                instr(group_label, char(0)) = 0
            ),
            PRIMARY KEY(project_id, sample_id)
        );

        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (11, 'add_project_sample_registry', strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
    )sql");
}


void apply_version_twelve(SqliteConnection& connection) {
    connection.execute(R"sql(
        CREATE TABLE project_sample_bindings (
            project_id TEXT NOT NULL,
            sample_id TEXT NOT NULL,
            input_layout TEXT NOT NULL CHECK(input_layout IN (
                'single_fastq', 'paired_fastq', 'alignment', 'variants'
            )),
            primary_file_id TEXT NOT NULL
                REFERENCES managed_files(id) ON DELETE RESTRICT,
            secondary_file_id TEXT
                REFERENCES managed_files(id) ON DELETE RESTRICT,
            reference_file_id TEXT
                REFERENCES managed_files(id) ON DELETE RESTRICT,
            PRIMARY KEY(project_id, sample_id),
            FOREIGN KEY(project_id, sample_id)
                REFERENCES project_samples(project_id, sample_id)
                ON DELETE CASCADE,
            CHECK(
                (input_layout = 'paired_fastq' AND
                    secondary_file_id IS NOT NULL AND
                    secondary_file_id != primary_file_id) OR
                (input_layout != 'paired_fastq' AND secondary_file_id IS NULL)
            ),
            CHECK(
                reference_file_id IS NULL OR (
                    reference_file_id != primary_file_id AND
                    (secondary_file_id IS NULL OR
                     reference_file_id != secondary_file_id)
                )
            )
        );

        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (12, 'bind_samples_to_inputs_and_references',
                strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
    )sql");
}


void apply_version_thirteen(SqliteConnection& connection) {
    connection.execute(R"sql(
        CREATE TABLE batch_plans (
            plan_id TEXT PRIMARY KEY NOT NULL CHECK(
                length(CAST(plan_id AS BLOB)) BETWEEN 1 AND 128 AND
                instr(plan_id, char(0)) = 0
            ),
            project_id TEXT NOT NULL CHECK(
                length(CAST(project_id AS BLOB)) BETWEEN 1 AND 128 AND
                instr(project_id, char(0)) = 0
            ),
            template_id TEXT NOT NULL CHECK(
                length(CAST(template_id AS BLOB)) BETWEEN 1 AND 256 AND
                instr(template_id, char(0)) = 0
            ),
            template_version TEXT NOT NULL CHECK(
                length(CAST(template_version AS BLOB)) BETWEEN 1 AND 64 AND
                instr(template_version, char(0)) = 0
            ),
            approved_at_utc TEXT NOT NULL CHECK(
                length(CAST(approved_at_utc AS BLOB)) BETWEEN 1 AND 200 AND
                instr(approved_at_utc, char(0)) = 0
            ),
            sealed INTEGER NOT NULL DEFAULT 0 CHECK(sealed IN (0, 1))
        );

        CREATE TABLE batch_plan_samples (
            plan_id TEXT NOT NULL
                REFERENCES batch_plans(plan_id) ON DELETE CASCADE,
            sample_id TEXT NOT NULL CHECK(
                length(CAST(sample_id AS BLOB)) BETWEEN 1 AND 128 AND
                instr(sample_id, char(0)) = 0
            ),
            ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
            disposition TEXT NOT NULL CHECK(disposition IN ('included', 'excluded')),
            workflow_id TEXT CHECK(
                workflow_id IS NULL OR (
                    length(CAST(workflow_id AS BLOB)) BETWEEN 1 AND 256 AND
                    instr(workflow_id, char(0)) = 0
                )
            ),
            PRIMARY KEY(plan_id, sample_id),
            UNIQUE(plan_id, ordinal),
            CHECK(
                (disposition = 'included' AND workflow_id IS NOT NULL) OR
                (disposition = 'excluded' AND workflow_id IS NULL)
            )
        );

        CREATE TABLE batch_plan_nodes (
            plan_id TEXT NOT NULL,
            sample_id TEXT NOT NULL,
            ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
            node_id TEXT NOT NULL CHECK(
                length(CAST(node_id AS BLOB)) BETWEEN 1 AND 128 AND
                instr(node_id, char(0)) = 0
            ),
            module_id TEXT NOT NULL CHECK(
                length(CAST(module_id AS BLOB)) BETWEEN 1 AND 256 AND
                instr(module_id, char(0)) = 0
            ),
            plugin_version TEXT NOT NULL CHECK(
                length(CAST(plugin_version AS BLOB)) BETWEEN 1 AND 64 AND
                instr(plugin_version, char(0)) = 0
            ),
            PRIMARY KEY(plan_id, sample_id, node_id),
            UNIQUE(plan_id, sample_id, ordinal),
            FOREIGN KEY(plan_id, sample_id)
                REFERENCES batch_plan_samples(plan_id, sample_id)
                ON DELETE CASCADE
        );

        CREATE TABLE batch_plan_parameters (
            plan_id TEXT NOT NULL,
            sample_id TEXT NOT NULL,
            node_id TEXT NOT NULL,
            ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
            name TEXT NOT NULL CHECK(
                length(CAST(name AS BLOB)) BETWEEN 1 AND 64 AND
                instr(name, char(0)) = 0
            ),
            parameter_type TEXT NOT NULL CHECK(parameter_type IN (
                'string', 'integer', 'number', 'boolean', 'enum'
            )),
            value TEXT NOT NULL CHECK(
                length(CAST(value AS BLOB)) <= 4096 AND
                instr(value, char(0)) = 0
            ),
            source TEXT NOT NULL CHECK(source IN (
                'node_literal', 'workflow_parameter', 'module_default'
            )),
            PRIMARY KEY(plan_id, sample_id, node_id, name),
            UNIQUE(plan_id, sample_id, node_id, ordinal),
            FOREIGN KEY(plan_id, sample_id, node_id)
                REFERENCES batch_plan_nodes(plan_id, sample_id, node_id)
                ON DELETE CASCADE
        );

        CREATE TABLE batch_plan_inputs (
            plan_id TEXT NOT NULL,
            sample_id TEXT NOT NULL,
            node_id TEXT NOT NULL,
            ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
            port_name TEXT NOT NULL CHECK(
                length(CAST(port_name AS BLOB)) BETWEEN 1 AND 64 AND
                instr(port_name, char(0)) = 0
            ),
            artifact_type TEXT NOT NULL CHECK(
                length(CAST(artifact_type AS BLOB)) BETWEEN 1 AND 128 AND
                instr(artifact_type, char(0)) = 0
            ),
            source_kind TEXT NOT NULL CHECK(source_kind IN (
                'managed_file', 'node_output'
            )),
            source_id TEXT NOT NULL CHECK(
                length(CAST(source_id AS BLOB)) BETWEEN 1 AND 256 AND
                instr(source_id, char(0)) = 0
            ),
            source_port TEXT NOT NULL DEFAULT '' CHECK(
                length(CAST(source_port AS BLOB)) <= 64 AND
                instr(source_port, char(0)) = 0
            ),
            file_role TEXT CHECK(file_role IS NULL OR file_role IN (
                'primary', 'secondary', 'reference'
            )),
            file_id TEXT,
            file_type TEXT,
            size_bytes INTEGER,
            sha256 TEXT,
            PRIMARY KEY(plan_id, sample_id, node_id, port_name),
            UNIQUE(plan_id, sample_id, node_id, ordinal),
            FOREIGN KEY(plan_id, sample_id, node_id)
                REFERENCES batch_plan_nodes(plan_id, sample_id, node_id)
                ON DELETE CASCADE,
            CHECK(
                (source_kind = 'managed_file' AND
                    source_port = '' AND
                    file_role IS NOT NULL AND
                    file_id = source_id AND
                    file_type IS NOT NULL AND
                    length(CAST(file_type AS BLOB)) BETWEEN 1 AND 128 AND
                    instr(file_type, char(0)) = 0 AND
                    size_bytes IS NOT NULL AND size_bytes >= 0 AND
                    sha256 IS NOT NULL AND
                    length(sha256) = 64 AND
                    sha256 NOT GLOB '*[^0-9a-f]*') OR
                (source_kind = 'node_output' AND
                    length(source_port) BETWEEN 1 AND 64 AND
                    file_role IS NULL AND file_id IS NULL AND
                    file_type IS NULL AND size_bytes IS NULL AND sha256 IS NULL)
            )
        );

        CREATE TABLE batch_plan_outputs (
            plan_id TEXT NOT NULL,
            sample_id TEXT NOT NULL,
            node_id TEXT NOT NULL,
            ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
            port_name TEXT NOT NULL CHECK(
                length(CAST(port_name AS BLOB)) BETWEEN 1 AND 64 AND
                instr(port_name, char(0)) = 0
            ),
            artifact_type TEXT NOT NULL CHECK(
                length(CAST(artifact_type AS BLOB)) BETWEEN 1 AND 128 AND
                instr(artifact_type, char(0)) = 0
            ),
            PRIMARY KEY(plan_id, sample_id, node_id, port_name),
            UNIQUE(plan_id, sample_id, node_id, ordinal),
            FOREIGN KEY(plan_id, sample_id, node_id)
                REFERENCES batch_plan_nodes(plan_id, sample_id, node_id)
                ON DELETE CASCADE
        );

        CREATE TRIGGER batch_plans_validate_seal
        BEFORE UPDATE ON batch_plans
        WHEN NOT (
            OLD.sealed = 0 AND NEW.sealed = 1 AND
            NEW.plan_id = OLD.plan_id AND
            NEW.project_id = OLD.project_id AND
            NEW.template_id = OLD.template_id AND
            NEW.template_version = OLD.template_version AND
            NEW.approved_at_utc = OLD.approved_at_utc AND
            EXISTS (
                SELECT 1 FROM batch_plan_samples
                WHERE plan_id = OLD.plan_id AND disposition = 'included'
            )
        )
        BEGIN
            SELECT RAISE(ABORT, 'approved batch plan is immutable');
        END;

        CREATE TRIGGER batch_plans_immutable_delete
        BEFORE DELETE ON batch_plans
        BEGIN
            SELECT RAISE(ABORT, 'approved batch plan cannot be deleted');
        END;

        CREATE TRIGGER batch_plan_samples_immutable_insert
        BEFORE INSERT ON batch_plan_samples
        WHEN (SELECT sealed FROM batch_plans WHERE plan_id = NEW.plan_id) = 1
        BEGIN
            SELECT RAISE(ABORT, 'sealed batch plan samples are immutable');
        END;
        CREATE TRIGGER batch_plan_samples_immutable_update
        BEFORE UPDATE ON batch_plan_samples
        BEGIN
            SELECT RAISE(ABORT, 'batch plan samples are immutable');
        END;
        CREATE TRIGGER batch_plan_samples_immutable_delete
        BEFORE DELETE ON batch_plan_samples
        BEGIN
            SELECT RAISE(ABORT, 'batch plan samples are immutable');
        END;

        CREATE TRIGGER batch_plan_nodes_immutable_insert
        BEFORE INSERT ON batch_plan_nodes
        WHEN (SELECT sealed FROM batch_plans WHERE plan_id = NEW.plan_id) = 1
        BEGIN
            SELECT RAISE(ABORT, 'sealed batch plan nodes are immutable');
        END;
        CREATE TRIGGER batch_plan_nodes_immutable_update
        BEFORE UPDATE ON batch_plan_nodes
        BEGIN
            SELECT RAISE(ABORT, 'batch plan nodes are immutable');
        END;
        CREATE TRIGGER batch_plan_nodes_immutable_delete
        BEFORE DELETE ON batch_plan_nodes
        BEGIN
            SELECT RAISE(ABORT, 'batch plan nodes are immutable');
        END;

        CREATE TRIGGER batch_plan_parameters_immutable_insert
        BEFORE INSERT ON batch_plan_parameters
        WHEN (SELECT sealed FROM batch_plans WHERE plan_id = NEW.plan_id) = 1
        BEGIN
            SELECT RAISE(ABORT, 'sealed batch plan parameters are immutable');
        END;
        CREATE TRIGGER batch_plan_parameters_immutable_update
        BEFORE UPDATE ON batch_plan_parameters
        BEGIN
            SELECT RAISE(ABORT, 'batch plan parameters are immutable');
        END;
        CREATE TRIGGER batch_plan_parameters_immutable_delete
        BEFORE DELETE ON batch_plan_parameters
        BEGIN
            SELECT RAISE(ABORT, 'batch plan parameters are immutable');
        END;

        CREATE TRIGGER batch_plan_inputs_immutable_insert
        BEFORE INSERT ON batch_plan_inputs
        WHEN (SELECT sealed FROM batch_plans WHERE plan_id = NEW.plan_id) = 1
        BEGIN
            SELECT RAISE(ABORT, 'sealed batch plan inputs are immutable');
        END;
        CREATE TRIGGER batch_plan_inputs_immutable_update
        BEFORE UPDATE ON batch_plan_inputs
        BEGIN
            SELECT RAISE(ABORT, 'batch plan inputs are immutable');
        END;
        CREATE TRIGGER batch_plan_inputs_immutable_delete
        BEFORE DELETE ON batch_plan_inputs
        BEGIN
            SELECT RAISE(ABORT, 'batch plan inputs are immutable');
        END;

        CREATE TRIGGER batch_plan_outputs_immutable_insert
        BEFORE INSERT ON batch_plan_outputs
        WHEN (SELECT sealed FROM batch_plans WHERE plan_id = NEW.plan_id) = 1
        BEGIN
            SELECT RAISE(ABORT, 'sealed batch plan outputs are immutable');
        END;
        CREATE TRIGGER batch_plan_outputs_immutable_update
        BEFORE UPDATE ON batch_plan_outputs
        BEGIN
            SELECT RAISE(ABORT, 'batch plan outputs are immutable');
        END;
        CREATE TRIGGER batch_plan_outputs_immutable_delete
        BEFORE DELETE ON batch_plan_outputs
        BEGIN
            SELECT RAISE(ABORT, 'batch plan outputs are immutable');
        END;

        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (13, 'persist_immutable_batch_plans',
                strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
    )sql");
}


void apply_version_fourteen(SqliteConnection& connection) {
    connection.execute(R"sql(
        CREATE TABLE batch_executions (
            plan_id TEXT PRIMARY KEY NOT NULL
                REFERENCES batch_plans(plan_id) ON DELETE RESTRICT,
            maximum_concurrent_jobs INTEGER NOT NULL
                CHECK(maximum_concurrent_jobs BETWEEN 1 AND 64),
            cancellation_requested INTEGER NOT NULL DEFAULT 0
                CHECK(cancellation_requested IN (0, 1)),
            submitted_at_utc TEXT NOT NULL CHECK(
                length(CAST(submitted_at_utc AS BLOB)) BETWEEN 1 AND 200 AND
                instr(submitted_at_utc, char(0)) = 0
            ),
            updated_at_utc TEXT NOT NULL CHECK(
                length(CAST(updated_at_utc AS BLOB)) BETWEEN 1 AND 200 AND
                instr(updated_at_utc, char(0)) = 0
            ),
            sealed INTEGER NOT NULL DEFAULT 0 CHECK(sealed IN (0, 1))
        );

        CREATE TABLE batch_execution_jobs (
            plan_id TEXT NOT NULL
                REFERENCES batch_executions(plan_id) ON DELETE CASCADE,
            sample_id TEXT NOT NULL CHECK(
                length(CAST(sample_id AS BLOB)) BETWEEN 1 AND 128 AND
                instr(sample_id, char(0)) = 0
            ),
            ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
            job_id TEXT NOT NULL UNIQUE
                REFERENCES jobs(id) ON DELETE RESTRICT,
            PRIMARY KEY(plan_id, sample_id),
            UNIQUE(plan_id, ordinal),
            FOREIGN KEY(plan_id, sample_id)
                REFERENCES batch_plan_samples(plan_id, sample_id)
                ON DELETE RESTRICT
        );

        CREATE INDEX idx_batch_execution_jobs_job_id
            ON batch_execution_jobs(job_id);

        CREATE TRIGGER batch_executions_require_sealed_plan
        BEFORE INSERT ON batch_executions
        WHEN NOT EXISTS (
            SELECT 1 FROM batch_plans
            WHERE plan_id = NEW.plan_id AND sealed = 1
        )
        BEGIN
            SELECT RAISE(ABORT, 'batch execution requires a sealed approved plan');
        END;

        CREATE TRIGGER batch_executions_validate_update
        BEFORE UPDATE ON batch_executions
        WHEN NOT (
            (
                OLD.sealed = 0 AND NEW.sealed = 1 AND
                NEW.plan_id = OLD.plan_id AND
                NEW.maximum_concurrent_jobs = OLD.maximum_concurrent_jobs AND
                NEW.cancellation_requested = OLD.cancellation_requested AND
                NEW.submitted_at_utc = OLD.submitted_at_utc AND
                NEW.updated_at_utc = OLD.updated_at_utc AND
                EXISTS (
                    SELECT 1 FROM batch_execution_jobs
                    WHERE plan_id = OLD.plan_id
                )
            ) OR (
                OLD.sealed = 1 AND NEW.sealed = 1 AND
                OLD.cancellation_requested = 0 AND
                NEW.cancellation_requested = 1 AND
                NEW.plan_id = OLD.plan_id AND
                NEW.maximum_concurrent_jobs = OLD.maximum_concurrent_jobs AND
                NEW.submitted_at_utc = OLD.submitted_at_utc AND
                length(CAST(NEW.updated_at_utc AS BLOB)) BETWEEN 1 AND 200 AND
                instr(NEW.updated_at_utc, char(0)) = 0
            )
        )
        BEGIN
            SELECT RAISE(ABORT, 'batch execution identity and submission ledger are immutable');
        END;

        CREATE TRIGGER batch_executions_immutable_delete
        BEFORE DELETE ON batch_executions
        BEGIN
            SELECT RAISE(ABORT, 'batch execution cannot be deleted');
        END;

        CREATE TRIGGER batch_execution_jobs_require_included_sample
        BEFORE INSERT ON batch_execution_jobs
        WHEN NOT EXISTS (
            SELECT 1 FROM batch_plan_samples
            WHERE plan_id = NEW.plan_id
              AND sample_id = NEW.sample_id
              AND disposition = 'included'
        )
        BEGIN
            SELECT RAISE(ABORT, 'batch execution job requires an included approved sample');
        END;

        CREATE TRIGGER batch_execution_jobs_immutable_insert
        BEFORE INSERT ON batch_execution_jobs
        WHEN (SELECT sealed FROM batch_executions WHERE plan_id = NEW.plan_id) = 1
        BEGIN
            SELECT RAISE(ABORT, 'sealed batch execution jobs are immutable');
        END;

        CREATE TRIGGER batch_execution_jobs_immutable_update
        BEFORE UPDATE ON batch_execution_jobs
        BEGIN
            SELECT RAISE(ABORT, 'batch execution jobs are immutable');
        END;

        CREATE TRIGGER batch_execution_jobs_immutable_delete
        BEFORE DELETE ON batch_execution_jobs
        BEGIN
            SELECT RAISE(ABORT, 'batch execution jobs are immutable');
        END;

        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (14, 'submit_batch_plans_through_scheduler',
                strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
    )sql");
}

void apply_version_fifteen(SqliteConnection& connection) {
    connection.execute(R"sql(
        CREATE TABLE batch_execution_attempts (
            plan_id TEXT NOT NULL,
            sample_id TEXT NOT NULL CHECK(
                length(CAST(sample_id AS BLOB)) BETWEEN 1 AND 128 AND
                instr(sample_id, char(0)) = 0
            ),
            attempt_number INTEGER NOT NULL CHECK(attempt_number BETWEEN 1 AND 64),
            job_id TEXT NOT NULL UNIQUE REFERENCES jobs(id) ON DELETE RESTRICT,
            parent_job_id TEXT REFERENCES jobs(id) ON DELETE RESTRICT,
            mode TEXT NOT NULL CHECK(mode IN ('initial', 'resume', 'retry')),
            created_at_utc TEXT NOT NULL CHECK(
                length(CAST(created_at_utc AS BLOB)) BETWEEN 1 AND 200 AND
                instr(created_at_utc, char(0)) = 0
            ),
            PRIMARY KEY(plan_id, sample_id, attempt_number),
            FOREIGN KEY(plan_id, sample_id)
                REFERENCES batch_execution_jobs(plan_id, sample_id)
                ON DELETE RESTRICT
        );

        CREATE TABLE batch_execution_attempt_nodes (
            plan_id TEXT NOT NULL,
            sample_id TEXT NOT NULL,
            attempt_number INTEGER NOT NULL,
            ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
            node_id TEXT NOT NULL CHECK(
                length(CAST(node_id AS BLOB)) BETWEEN 1 AND 128 AND
                instr(node_id, char(0)) = 0
            ),
            PRIMARY KEY(plan_id, sample_id, attempt_number, node_id),
            UNIQUE(plan_id, sample_id, attempt_number, ordinal),
            FOREIGN KEY(plan_id, sample_id, attempt_number)
                REFERENCES batch_execution_attempts(plan_id, sample_id, attempt_number)
                ON DELETE RESTRICT,
            FOREIGN KEY(plan_id, sample_id, node_id)
                REFERENCES batch_plan_nodes(plan_id, sample_id, node_id)
                ON DELETE RESTRICT
        );

        CREATE INDEX idx_batch_execution_attempts_job_id
            ON batch_execution_attempts(job_id);
        CREATE INDEX idx_batch_execution_attempts_latest
            ON batch_execution_attempts(plan_id, sample_id, attempt_number DESC);

        CREATE TRIGGER batch_execution_attempts_validate_insert
        BEFORE INSERT ON batch_execution_attempts
        WHEN
            (
                NEW.attempt_number > 1 AND
                (SELECT cancellation_requested FROM batch_executions
                 WHERE plan_id = NEW.plan_id) != 0
            ) OR
            NOT (
                (
                    NEW.attempt_number = 1 AND
                    NEW.mode = 'initial' AND
                    NEW.parent_job_id IS NULL AND
                    EXISTS (
                        SELECT 1 FROM batch_execution_jobs
                        WHERE plan_id = NEW.plan_id
                          AND sample_id = NEW.sample_id
                          AND job_id = NEW.job_id
                    )
                ) OR (
                    NEW.attempt_number > 1 AND
                    NEW.mode IN ('resume', 'retry') AND
                    NEW.parent_job_id = (
                        SELECT job_id FROM batch_execution_attempts
                        WHERE plan_id = NEW.plan_id
                          AND sample_id = NEW.sample_id
                          AND attempt_number = NEW.attempt_number - 1
                    ) AND
                    NEW.attempt_number = COALESCE((
                        SELECT MAX(attempt_number) + 1
                        FROM batch_execution_attempts
                        WHERE plan_id = NEW.plan_id
                          AND sample_id = NEW.sample_id
                    ), 1)
                )
            )
        BEGIN
            SELECT RAISE(ABORT, 'invalid batch execution attempt lineage');
        END;

        CREATE TRIGGER batch_execution_attempts_immutable_update
        BEFORE UPDATE ON batch_execution_attempts
        BEGIN
            SELECT RAISE(ABORT, 'batch execution attempt lineage is immutable');
        END;

        CREATE TRIGGER batch_execution_attempts_immutable_delete
        BEFORE DELETE ON batch_execution_attempts
        BEGIN
            SELECT RAISE(ABORT, 'batch execution attempt lineage cannot be deleted');
        END;

        CREATE TRIGGER batch_execution_attempt_nodes_validate_insert
        BEFORE INSERT ON batch_execution_attempt_nodes
        WHEN NOT EXISTS (
            SELECT 1 FROM batch_plan_nodes
            WHERE plan_id = NEW.plan_id
              AND sample_id = NEW.sample_id
              AND node_id = NEW.node_id
        )
        BEGIN
            SELECT RAISE(ABORT, 'batch attempt node must belong to the frozen approved plan');
        END;

        CREATE TRIGGER batch_execution_attempt_nodes_immutable_update
        BEFORE UPDATE ON batch_execution_attempt_nodes
        BEGIN
            SELECT RAISE(ABORT, 'batch execution attempt nodes are immutable');
        END;

        CREATE TRIGGER batch_execution_attempt_nodes_immutable_delete
        BEFORE DELETE ON batch_execution_attempt_nodes
        BEGIN
            SELECT RAISE(ABORT, 'batch execution attempt nodes cannot be deleted');
        END;

        INSERT INTO batch_execution_attempts(
            plan_id,sample_id,attempt_number,job_id,parent_job_id,mode,created_at_utc
        )
        SELECT j.plan_id,j.sample_id,1,j.job_id,NULL,'initial',e.submitted_at_utc
        FROM batch_execution_jobs AS j
        JOIN batch_executions AS e ON e.plan_id=j.plan_id;

        INSERT INTO batch_execution_attempt_nodes(
            plan_id,sample_id,attempt_number,ordinal,node_id
        )
        SELECT a.plan_id,a.sample_id,1,n.ordinal,n.node_id
        FROM batch_execution_attempts AS a
        JOIN batch_plan_nodes AS n
          ON n.plan_id=a.plan_id AND n.sample_id=a.sample_id
        WHERE a.attempt_number=1
        ORDER BY a.plan_id,a.sample_id,n.ordinal;

        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (15, 'add_batch_recovery_attempt_lineage',
                strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
    )sql");
}

void apply_version_sixteen(SqliteConnection& connection) {
    connection.execute(R"sql(
        CREATE TABLE cohort_definitions (
            project_id TEXT NOT NULL
                REFERENCES project_metadata(project_id) ON DELETE RESTRICT,
            cohort_id TEXT NOT NULL CHECK(
                length(CAST(cohort_id AS BLOB)) BETWEEN 1 AND 128 AND
                instr(cohort_id, char(0)) = 0
            ),
            name TEXT NOT NULL CHECK(
                length(CAST(name AS BLOB)) BETWEEN 1 AND 200 AND
                length(trim(name)) > 0 AND
                instr(name, char(0)) = 0
            ),
            current_revision INTEGER NOT NULL DEFAULT 0 CHECK(current_revision >= 0),
            created_at_utc TEXT NOT NULL CHECK(
                length(CAST(created_at_utc AS BLOB)) BETWEEN 1 AND 200 AND
                instr(created_at_utc, char(0)) = 0
            ),
            updated_at_utc TEXT NOT NULL CHECK(
                length(CAST(updated_at_utc AS BLOB)) BETWEEN 1 AND 200 AND
                instr(updated_at_utc, char(0)) = 0
            ),
            PRIMARY KEY(project_id, cohort_id)
        );

        CREATE TABLE cohort_revisions (
            project_id TEXT NOT NULL,
            cohort_id TEXT NOT NULL,
            revision INTEGER NOT NULL CHECK(revision >= 1),
            parent_revision INTEGER,
            created_at_utc TEXT NOT NULL CHECK(
                length(CAST(created_at_utc AS BLOB)) BETWEEN 1 AND 200 AND
                instr(created_at_utc, char(0)) = 0
            ),
            sealed INTEGER NOT NULL DEFAULT 0 CHECK(sealed IN (0, 1)),
            PRIMARY KEY(project_id, cohort_id, revision),
            FOREIGN KEY(project_id, cohort_id)
                REFERENCES cohort_definitions(project_id, cohort_id)
                ON DELETE RESTRICT,
            FOREIGN KEY(project_id, cohort_id, parent_revision)
                REFERENCES cohort_revisions(project_id, cohort_id, revision)
                ON DELETE RESTRICT,
            CHECK(
                (revision = 1 AND parent_revision IS NULL) OR
                (revision > 1 AND parent_revision = revision - 1)
            )
        );

        CREATE TABLE cohort_revision_members (
            project_id TEXT NOT NULL,
            cohort_id TEXT NOT NULL,
            revision INTEGER NOT NULL CHECK(revision >= 1),
            sample_id TEXT NOT NULL CHECK(
                length(CAST(sample_id AS BLOB)) BETWEEN 1 AND 128 AND
                instr(sample_id, char(0)) = 0
            ),
            ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
            sample_display_name TEXT NOT NULL DEFAULT '' CHECK(
                length(CAST(sample_display_name AS BLOB)) <= 255 AND
                instr(sample_display_name, char(0)) = 0
            ),
            sample_group_metadata TEXT NOT NULL DEFAULT '' CHECK(
                length(CAST(sample_group_metadata AS BLOB)) <= 128 AND
                instr(sample_group_metadata, char(0)) = 0
            ),
            biological_unit_id TEXT NOT NULL CHECK(
                length(CAST(biological_unit_id AS BLOB)) BETWEEN 1 AND 128 AND
                instr(biological_unit_id, char(0)) = 0
            ),
            group_token TEXT NOT NULL CHECK(group_token IN ('case', 'control', 'unassigned')),
            disposition TEXT NOT NULL CHECK(disposition IN ('included', 'excluded')),
            exclusion_reason TEXT CHECK(
                exclusion_reason IS NULL OR (
                    length(CAST(exclusion_reason AS BLOB)) BETWEEN 1 AND 512 AND
                    length(trim(exclusion_reason)) > 0 AND
                    instr(exclusion_reason, char(0)) = 0
                )
            ),
            PRIMARY KEY(project_id, cohort_id, revision, sample_id),
            UNIQUE(project_id, cohort_id, revision, ordinal),
            FOREIGN KEY(project_id, cohort_id, revision)
                REFERENCES cohort_revisions(project_id, cohort_id, revision)
                ON DELETE RESTRICT,
            FOREIGN KEY(project_id, sample_id)
                REFERENCES project_samples(project_id, sample_id)
                ON DELETE RESTRICT,
            CHECK(
                (disposition = 'included' AND exclusion_reason IS NULL) OR
                (disposition = 'excluded' AND exclusion_reason IS NOT NULL)
            )
        );

        CREATE INDEX idx_cohort_definitions_project_created
            ON cohort_definitions(project_id, created_at_utc, cohort_id);
        CREATE INDEX idx_cohort_revision_members_sample
            ON cohort_revision_members(project_id, sample_id);

        CREATE TRIGGER cohort_definitions_validate_update
        BEFORE UPDATE ON cohort_definitions
        WHEN NOT (
            NEW.project_id = OLD.project_id AND
            NEW.cohort_id = OLD.cohort_id AND
            NEW.name = OLD.name AND
            NEW.created_at_utc = OLD.created_at_utc AND
            NEW.current_revision = OLD.current_revision + 1 AND
            length(CAST(NEW.updated_at_utc AS BLOB)) BETWEEN 1 AND 200 AND
            instr(NEW.updated_at_utc, char(0)) = 0 AND
            EXISTS (
                SELECT 1 FROM cohort_revisions
                WHERE project_id = OLD.project_id
                  AND cohort_id = OLD.cohort_id
                  AND revision = NEW.current_revision
                  AND sealed = 1
            )
        )
        BEGIN
            SELECT RAISE(ABORT, 'cohort definition revision may advance only to a sealed revision');
        END;

        CREATE TRIGGER cohort_definitions_immutable_delete
        BEFORE DELETE ON cohort_definitions
        BEGIN
            SELECT RAISE(ABORT, 'cohort definition cannot be deleted');
        END;

        CREATE TRIGGER cohort_revisions_validate_update
        BEFORE UPDATE ON cohort_revisions
        WHEN NOT (
            OLD.sealed = 0 AND NEW.sealed = 1 AND
            NEW.project_id = OLD.project_id AND
            NEW.cohort_id = OLD.cohort_id AND
            NEW.revision = OLD.revision AND
            NEW.parent_revision IS OLD.parent_revision AND
            NEW.created_at_utc = OLD.created_at_utc
        )
        BEGIN
            SELECT RAISE(ABORT, 'cohort revision is immutable after sealing');
        END;

        CREATE TRIGGER cohort_revisions_immutable_delete
        BEFORE DELETE ON cohort_revisions
        BEGIN
            SELECT RAISE(ABORT, 'cohort revision cannot be deleted');
        END;

        CREATE TRIGGER cohort_revision_members_require_open_revision
        BEFORE INSERT ON cohort_revision_members
        WHEN COALESCE((
            SELECT sealed FROM cohort_revisions
            WHERE project_id = NEW.project_id
              AND cohort_id = NEW.cohort_id
              AND revision = NEW.revision
        ), 1) != 0
        BEGIN
            SELECT RAISE(ABORT, 'cohort members require an open revision');
        END;

        CREATE TRIGGER cohort_revision_members_immutable_update
        BEFORE UPDATE ON cohort_revision_members
        BEGIN
            SELECT RAISE(ABORT, 'cohort revision members are immutable');
        END;

        CREATE TRIGGER cohort_revision_members_immutable_delete
        BEFORE DELETE ON cohort_revision_members
        BEGIN
            SELECT RAISE(ABORT, 'cohort revision members are immutable');
        END;

        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (16, 'add_cohort_registry_and_revisions',
                strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
    )sql");
}

void apply_version_seventeen(SqliteConnection& connection) {
    connection.execute(R"sql(
        CREATE TABLE cohort_analysis_snapshots (
            project_id TEXT NOT NULL
                REFERENCES project_metadata(project_id) ON DELETE RESTRICT,
            analysis_id TEXT NOT NULL CHECK(
                length(CAST(analysis_id AS BLOB)) BETWEEN 1 AND 128 AND
                instr(analysis_id, char(0)) = 0
            ),
            cohort_id TEXT NOT NULL,
            cohort_revision INTEGER NOT NULL CHECK(cohort_revision >= 1),
            contract_version TEXT NOT NULL CHECK(
                length(CAST(contract_version AS BLOB)) BETWEEN 1 AND 128 AND
                instr(contract_version, char(0)) = 0
            ),
            preview_digest TEXT NOT NULL CHECK(
                length(preview_digest) = 64 AND
                preview_digest NOT GLOB '*[^0-9a-f]*'
            ),
            snapshot_digest TEXT NOT NULL CHECK(
                length(snapshot_digest) = 64 AND
                snapshot_digest NOT GLOB '*[^0-9a-f]*'
            ),
            approved_at_utc TEXT NOT NULL CHECK(
                length(CAST(approved_at_utc AS BLOB)) BETWEEN 1 AND 200 AND
                instr(approved_at_utc, char(0)) = 0
            ),
            reference_file_id TEXT NOT NULL
                REFERENCES managed_files(id) ON DELETE RESTRICT,
            reference_file_type TEXT NOT NULL CHECK(reference_file_type = 'fasta'),
            reference_size_bytes INTEGER NOT NULL CHECK(reference_size_bytes >= 0),
            reference_sha256 TEXT NOT NULL CHECK(
                length(reference_sha256) = 64 AND
                reference_sha256 NOT GLOB '*[^0-9a-f]*'
            ),
            reference_assembly TEXT NOT NULL CHECK(
                reference_assembly IN ('grch37', 'grch38', 'custom')
            ),
            reference_custom_id TEXT CHECK(
                reference_custom_id IS NULL OR (
                    length(CAST(reference_custom_id AS BLOB)) BETWEEN 1 AND 256 AND
                    instr(reference_custom_id, char(0)) = 0
                )
            ),
            normalization_contract_version TEXT NOT NULL CHECK(
                length(CAST(normalization_contract_version AS BLOB)) BETWEEN 1 AND 128 AND
                instr(normalization_contract_version, char(0)) = 0
            ),
            matrix_stage_id TEXT NOT NULL CHECK(matrix_stage_id = 'matrix'),
            matrix_module_id TEXT NOT NULL CHECK(
                matrix_module_id = 'org.biocore.cohort.matrix'
            ),
            maximum_samples INTEGER NOT NULL CHECK(maximum_samples = 100),
            maximum_sources INTEGER NOT NULL CHECK(maximum_sources = 100),
            maximum_vcf_bytes INTEGER NOT NULL CHECK(maximum_vcf_bytes = 67108864),
            maximum_combined_vcf_bytes INTEGER NOT NULL CHECK(
                maximum_combined_vcf_bytes = 268435456
            ),
            maximum_reference_bytes INTEGER NOT NULL CHECK(
                maximum_reference_bytes = 536870912
            ),
            maximum_normalized_alleles INTEGER NOT NULL CHECK(
                maximum_normalized_alleles = 10000
            ),
            maximum_observations INTEGER NOT NULL CHECK(
                maximum_observations = 1000000
            ),
            association_contract_version TEXT NOT NULL CHECK(
                length(CAST(association_contract_version AS BLOB)) BETWEEN 1 AND 128 AND
                instr(association_contract_version, char(0)) = 0
            ),
            test_filter_version TEXT NOT NULL CHECK(
                length(CAST(test_filter_version AS BLOB)) BETWEEN 1 AND 128 AND
                instr(test_filter_version, char(0)) = 0
            ),
            minimum_complete_case_calls INTEGER NOT NULL CHECK(
                minimum_complete_case_calls BETWEEN 1 AND 100
            ),
            minimum_complete_control_calls INTEGER NOT NULL CHECK(
                minimum_complete_control_calls BETWEEN 1 AND 100
            ),
            maximum_fisher_table_states INTEGER NOT NULL CHECK(
                maximum_fisher_table_states BETWEEN 1 AND 100000
            ),
            approved_case_samples INTEGER NOT NULL CHECK(
                approved_case_samples BETWEEN 1 AND 100
            ),
            approved_control_samples INTEGER NOT NULL CHECK(
                approved_control_samples BETWEEN 1 AND 100
            ),
            allele_family_size INTEGER NOT NULL CHECK(
                allele_family_size BETWEEN 0 AND 10000
            ),
            carrier_family_size INTEGER NOT NULL CHECK(
                carrier_family_size BETWEEN 0 AND 10000
            ),
            sealed INTEGER NOT NULL DEFAULT 0 CHECK(sealed IN (0, 1)),
            PRIMARY KEY(project_id, analysis_id),
            UNIQUE(project_id, snapshot_digest),
            FOREIGN KEY(project_id, cohort_id, cohort_revision)
                REFERENCES cohort_revisions(project_id, cohort_id, revision)
                ON DELETE RESTRICT,
            CHECK(
                (reference_assembly = 'custom' AND reference_custom_id IS NOT NULL) OR
                (reference_assembly != 'custom' AND reference_custom_id IS NULL)
            )
        );

        CREATE TABLE cohort_analysis_samples (
            project_id TEXT NOT NULL,
            analysis_id TEXT NOT NULL,
            ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
            sample_id TEXT NOT NULL CHECK(
                length(CAST(sample_id AS BLOB)) BETWEEN 1 AND 128 AND
                instr(sample_id, char(0)) = 0
            ),
            sample_display_name TEXT NOT NULL DEFAULT '' CHECK(
                length(CAST(sample_display_name AS BLOB)) <= 255 AND
                instr(sample_display_name, char(0)) = 0
            ),
            sample_group_metadata TEXT NOT NULL DEFAULT '' CHECK(
                length(CAST(sample_group_metadata AS BLOB)) <= 128 AND
                instr(sample_group_metadata, char(0)) = 0
            ),
            biological_unit_id TEXT NOT NULL CHECK(
                length(CAST(biological_unit_id AS BLOB)) BETWEEN 1 AND 128 AND
                instr(biological_unit_id, char(0)) = 0
            ),
            group_token TEXT NOT NULL CHECK(group_token IN ('case', 'control', 'unassigned')),
            cohort_disposition TEXT NOT NULL CHECK(cohort_disposition IN ('included', 'excluded')),
            cohort_exclusion_reason TEXT CHECK(
                cohort_exclusion_reason IS NULL OR (
                    length(CAST(cohort_exclusion_reason AS BLOB)) BETWEEN 1 AND 512 AND
                    length(trim(cohort_exclusion_reason)) > 0 AND
                    instr(cohort_exclusion_reason, char(0)) = 0
                )
            ),
            analysis_disposition TEXT NOT NULL CHECK(analysis_disposition IN ('included', 'excluded')),
            analysis_reason TEXT CHECK(
                analysis_reason IS NULL OR (
                    length(CAST(analysis_reason AS BLOB)) BETWEEN 1 AND 512 AND
                    length(trim(analysis_reason)) > 0 AND
                    instr(analysis_reason, char(0)) = 0
                )
            ),
            qc_state TEXT NOT NULL CHECK(qc_state IN ('verified', 'unavailable', 'not_applicable')),
            qc_reason TEXT NOT NULL CHECK(
                length(CAST(qc_reason AS BLOB)) BETWEEN 1 AND 1024 AND
                instr(qc_reason, char(0)) = 0
            ),
            qc_file_id TEXT REFERENCES managed_files(id) ON DELETE RESTRICT,
            qc_job_id TEXT,
            qc_step_id TEXT,
            qc_output_port TEXT,
            qc_module_id TEXT,
            qc_plugin_version TEXT,
            qc_size_bytes INTEGER,
            qc_sha256 TEXT,
            PRIMARY KEY(project_id, analysis_id, sample_id),
            UNIQUE(project_id, analysis_id, ordinal),
            FOREIGN KEY(project_id, analysis_id)
                REFERENCES cohort_analysis_snapshots(project_id, analysis_id)
                ON DELETE RESTRICT,
            FOREIGN KEY(project_id, sample_id)
                REFERENCES project_samples(project_id, sample_id)
                ON DELETE RESTRICT,
            CHECK(
                (cohort_disposition = 'included' AND cohort_exclusion_reason IS NULL) OR
                (cohort_disposition = 'excluded' AND cohort_exclusion_reason IS NOT NULL)
            ),
            CHECK(
                cohort_disposition = 'included' OR analysis_disposition = 'excluded'
            ),
            CHECK(
                analysis_disposition = 'excluded' OR group_token IN ('case', 'control')
            ),
            CHECK(
                (analysis_disposition = 'excluded' AND analysis_reason IS NOT NULL) OR
                (analysis_disposition = 'included' AND
                    (qc_state = 'verified' OR analysis_reason IS NOT NULL))
            ),
            CHECK(
                (qc_state = 'verified' AND
                    qc_file_id IS NOT NULL AND qc_job_id IS NOT NULL AND
                    qc_step_id IS NOT NULL AND qc_output_port IS NOT NULL AND
                    qc_module_id IS NOT NULL AND qc_plugin_version IS NOT NULL AND
                    qc_size_bytes IS NOT NULL AND qc_size_bytes >= 0 AND
                    qc_sha256 IS NOT NULL AND length(qc_sha256) = 64 AND
                    qc_sha256 NOT GLOB '*[^0-9a-f]*') OR
                (qc_state != 'verified' AND
                    qc_file_id IS NULL AND qc_job_id IS NULL AND
                    qc_step_id IS NULL AND qc_output_port IS NULL AND
                    qc_module_id IS NULL AND qc_plugin_version IS NULL AND
                    qc_size_bytes IS NULL AND qc_sha256 IS NULL)
            )
        );

        CREATE TABLE cohort_analysis_sources (
            project_id TEXT NOT NULL,
            analysis_id TEXT NOT NULL,
            project_sample_id TEXT NOT NULL,
            biological_unit_id TEXT NOT NULL,
            plan_id TEXT NOT NULL,
            workflow_id TEXT NOT NULL,
            producer_sample_id TEXT NOT NULL,
            attempt_number INTEGER NOT NULL CHECK(attempt_number >= 1),
            job_id TEXT NOT NULL,
            step_id TEXT NOT NULL,
            output_port TEXT NOT NULL,
            module_id TEXT NOT NULL,
            plugin_version TEXT NOT NULL,
            managed_file_id TEXT NOT NULL
                REFERENCES managed_files(id) ON DELETE RESTRICT,
            size_bytes INTEGER NOT NULL CHECK(size_bytes >= 0),
            sha256 TEXT NOT NULL CHECK(
                length(sha256) = 64 AND sha256 NOT GLOB '*[^0-9a-f]*'
            ),
            vcf_sample_name TEXT NOT NULL CHECK(
                length(CAST(vcf_sample_name AS BLOB)) BETWEEN 1 AND 1024 AND
                instr(vcf_sample_name, char(0)) = 0
            ),
            PRIMARY KEY(project_id, analysis_id, project_sample_id),
            FOREIGN KEY(project_id, analysis_id)
                REFERENCES cohort_analysis_snapshots(project_id, analysis_id)
                ON DELETE RESTRICT,
            FOREIGN KEY(project_id, project_sample_id)
                REFERENCES project_samples(project_id, sample_id)
                ON DELETE RESTRICT
        );

        CREATE TABLE cohort_analysis_reference_contigs (
            project_id TEXT NOT NULL,
            analysis_id TEXT NOT NULL,
            ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
            canonical_name TEXT NOT NULL CHECK(
                length(CAST(canonical_name AS BLOB)) BETWEEN 1 AND 4096 AND
                instr(canonical_name, char(0)) = 0
            ),
            length_bases INTEGER NOT NULL CHECK(length_bases >= 1),
            PRIMARY KEY(project_id, analysis_id, ordinal),
            UNIQUE(project_id, analysis_id, canonical_name),
            FOREIGN KEY(project_id, analysis_id)
                REFERENCES cohort_analysis_snapshots(project_id, analysis_id)
                ON DELETE RESTRICT
        );

        CREATE TABLE cohort_analysis_reference_aliases (
            project_id TEXT NOT NULL,
            analysis_id TEXT NOT NULL,
            ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
            alias TEXT NOT NULL CHECK(
                length(CAST(alias AS BLOB)) BETWEEN 1 AND 4096 AND
                instr(alias, char(0)) = 0
            ),
            canonical_name TEXT NOT NULL CHECK(
                length(CAST(canonical_name AS BLOB)) BETWEEN 1 AND 4096 AND
                instr(canonical_name, char(0)) = 0
            ),
            PRIMARY KEY(project_id, analysis_id, ordinal),
            UNIQUE(project_id, analysis_id, alias),
            FOREIGN KEY(project_id, analysis_id)
                REFERENCES cohort_analysis_snapshots(project_id, analysis_id)
                ON DELETE RESTRICT
        );

        CREATE TABLE cohort_analysis_test_universe (
            project_id TEXT NOT NULL,
            analysis_id TEXT NOT NULL,
            ordinal INTEGER NOT NULL CHECK(ordinal >= 0),
            contig TEXT NOT NULL CHECK(
                length(CAST(contig AS BLOB)) BETWEEN 1 AND 4096 AND
                instr(contig, char(0)) = 0
            ),
            start_pos INTEGER NOT NULL CHECK(start_pos >= 0),
            end_pos INTEGER NOT NULL CHECK(end_pos >= start_pos),
            reference_allele TEXT NOT NULL CHECK(
                length(CAST(reference_allele AS BLOB)) BETWEEN 1 AND 100000 AND
                instr(reference_allele, char(0)) = 0
            ),
            alternate_allele TEXT NOT NULL CHECK(
                length(CAST(alternate_allele AS BLOB)) BETWEEN 1 AND 100000 AND
                instr(alternate_allele, char(0)) = 0
            ),
            case_unobserved INTEGER NOT NULL CHECK(case_unobserved >= 0),
            control_unobserved INTEGER NOT NULL CHECK(control_unobserved >= 0),
            case_no_calls INTEGER NOT NULL CHECK(case_no_calls >= 0),
            control_no_calls INTEGER NOT NULL CHECK(control_no_calls >= 0),
            case_partial_calls INTEGER NOT NULL CHECK(case_partial_calls >= 0),
            control_partial_calls INTEGER NOT NULL CHECK(control_partial_calls >= 0),
            case_complete_calls INTEGER NOT NULL CHECK(case_complete_calls >= 0),
            control_complete_calls INTEGER NOT NULL CHECK(control_complete_calls >= 0),
            allele_family_member INTEGER NOT NULL CHECK(allele_family_member IN (0, 1)),
            carrier_family_member INTEGER NOT NULL CHECK(carrier_family_member IN (0, 1)),
            PRIMARY KEY(project_id, analysis_id, ordinal),
            FOREIGN KEY(project_id, analysis_id)
                REFERENCES cohort_analysis_snapshots(project_id, analysis_id)
                ON DELETE RESTRICT
        );

        CREATE INDEX idx_cohort_analysis_snapshots_cohort
            ON cohort_analysis_snapshots(project_id, cohort_id, cohort_revision, approved_at_utc);
        CREATE INDEX idx_cohort_analysis_sources_file
            ON cohort_analysis_sources(managed_file_id);
        CREATE INDEX idx_cohort_analysis_samples_sample
            ON cohort_analysis_samples(project_id, sample_id);

        CREATE TRIGGER cohort_analysis_snapshots_validate_update
        BEFORE UPDATE ON cohort_analysis_snapshots
        WHEN NOT (
            OLD.sealed = 0 AND NEW.sealed = 1 AND
            NEW.project_id = OLD.project_id AND
            NEW.analysis_id = OLD.analysis_id AND
            NEW.cohort_id = OLD.cohort_id AND
            NEW.cohort_revision = OLD.cohort_revision AND
            NEW.contract_version = OLD.contract_version AND
            NEW.preview_digest = OLD.preview_digest AND
            NEW.snapshot_digest = OLD.snapshot_digest AND
            NEW.approved_at_utc = OLD.approved_at_utc AND
            NEW.reference_file_id = OLD.reference_file_id AND
            NEW.reference_file_type = OLD.reference_file_type AND
            NEW.reference_size_bytes = OLD.reference_size_bytes AND
            NEW.reference_sha256 = OLD.reference_sha256 AND
            NEW.reference_assembly = OLD.reference_assembly AND
            NEW.reference_custom_id IS OLD.reference_custom_id AND
            NEW.normalization_contract_version = OLD.normalization_contract_version AND
            NEW.matrix_stage_id = OLD.matrix_stage_id AND
            NEW.matrix_module_id = OLD.matrix_module_id AND
            NEW.maximum_samples = OLD.maximum_samples AND
            NEW.maximum_sources = OLD.maximum_sources AND
            NEW.maximum_vcf_bytes = OLD.maximum_vcf_bytes AND
            NEW.maximum_combined_vcf_bytes = OLD.maximum_combined_vcf_bytes AND
            NEW.maximum_reference_bytes = OLD.maximum_reference_bytes AND
            NEW.maximum_normalized_alleles = OLD.maximum_normalized_alleles AND
            NEW.maximum_observations = OLD.maximum_observations AND
            NEW.association_contract_version = OLD.association_contract_version AND
            NEW.test_filter_version = OLD.test_filter_version AND
            NEW.minimum_complete_case_calls = OLD.minimum_complete_case_calls AND
            NEW.minimum_complete_control_calls = OLD.minimum_complete_control_calls AND
            NEW.maximum_fisher_table_states = OLD.maximum_fisher_table_states AND
            NEW.approved_case_samples = OLD.approved_case_samples AND
            NEW.approved_control_samples = OLD.approved_control_samples AND
            NEW.allele_family_size = OLD.allele_family_size AND
            NEW.carrier_family_size = OLD.carrier_family_size
        )
        BEGIN
            SELECT RAISE(ABORT, 'cohort analysis snapshot is immutable after creation');
        END;

        CREATE TRIGGER cohort_analysis_snapshots_immutable_delete
        BEFORE DELETE ON cohort_analysis_snapshots
        BEGIN
            SELECT RAISE(ABORT, 'cohort analysis snapshot cannot be deleted');
        END;

        CREATE TRIGGER cohort_analysis_samples_require_open_snapshot
        BEFORE INSERT ON cohort_analysis_samples
        WHEN COALESCE((
            SELECT sealed FROM cohort_analysis_snapshots
            WHERE project_id = NEW.project_id AND analysis_id = NEW.analysis_id
        ), 1) != 0
        BEGIN
            SELECT RAISE(ABORT, 'cohort analysis samples require an open snapshot');
        END;
        CREATE TRIGGER cohort_analysis_samples_immutable_update
        BEFORE UPDATE ON cohort_analysis_samples
        BEGIN SELECT RAISE(ABORT, 'cohort analysis samples are immutable'); END;
        CREATE TRIGGER cohort_analysis_samples_immutable_delete
        BEFORE DELETE ON cohort_analysis_samples
        BEGIN SELECT RAISE(ABORT, 'cohort analysis samples are immutable'); END;

        CREATE TRIGGER cohort_analysis_sources_require_open_snapshot
        BEFORE INSERT ON cohort_analysis_sources
        WHEN COALESCE((
            SELECT sealed FROM cohort_analysis_snapshots
            WHERE project_id = NEW.project_id AND analysis_id = NEW.analysis_id
        ), 1) != 0
        BEGIN
            SELECT RAISE(ABORT, 'cohort analysis sources require an open snapshot');
        END;
        CREATE TRIGGER cohort_analysis_sources_immutable_update
        BEFORE UPDATE ON cohort_analysis_sources
        BEGIN SELECT RAISE(ABORT, 'cohort analysis sources are immutable'); END;
        CREATE TRIGGER cohort_analysis_sources_immutable_delete
        BEFORE DELETE ON cohort_analysis_sources
        BEGIN SELECT RAISE(ABORT, 'cohort analysis sources are immutable'); END;

        CREATE TRIGGER cohort_analysis_reference_contigs_require_open_snapshot
        BEFORE INSERT ON cohort_analysis_reference_contigs
        WHEN COALESCE((
            SELECT sealed FROM cohort_analysis_snapshots
            WHERE project_id = NEW.project_id AND analysis_id = NEW.analysis_id
        ), 1) != 0
        BEGIN
            SELECT RAISE(ABORT, 'cohort analysis reference contigs require an open snapshot');
        END;
        CREATE TRIGGER cohort_analysis_reference_contigs_immutable_update
        BEFORE UPDATE ON cohort_analysis_reference_contigs
        BEGIN SELECT RAISE(ABORT, 'cohort analysis reference contigs are immutable'); END;
        CREATE TRIGGER cohort_analysis_reference_contigs_immutable_delete
        BEFORE DELETE ON cohort_analysis_reference_contigs
        BEGIN SELECT RAISE(ABORT, 'cohort analysis reference contigs are immutable'); END;

        CREATE TRIGGER cohort_analysis_reference_aliases_require_open_snapshot
        BEFORE INSERT ON cohort_analysis_reference_aliases
        WHEN COALESCE((
            SELECT sealed FROM cohort_analysis_snapshots
            WHERE project_id = NEW.project_id AND analysis_id = NEW.analysis_id
        ), 1) != 0
        BEGIN
            SELECT RAISE(ABORT, 'cohort analysis reference aliases require an open snapshot');
        END;
        CREATE TRIGGER cohort_analysis_reference_aliases_immutable_update
        BEFORE UPDATE ON cohort_analysis_reference_aliases
        BEGIN SELECT RAISE(ABORT, 'cohort analysis reference aliases are immutable'); END;
        CREATE TRIGGER cohort_analysis_reference_aliases_immutable_delete
        BEFORE DELETE ON cohort_analysis_reference_aliases
        BEGIN SELECT RAISE(ABORT, 'cohort analysis reference aliases are immutable'); END;

        CREATE TRIGGER cohort_analysis_test_universe_require_open_snapshot
        BEFORE INSERT ON cohort_analysis_test_universe
        WHEN COALESCE((
            SELECT sealed FROM cohort_analysis_snapshots
            WHERE project_id = NEW.project_id AND analysis_id = NEW.analysis_id
        ), 1) != 0
        BEGIN
            SELECT RAISE(ABORT, 'cohort analysis test universe requires an open snapshot');
        END;
        CREATE TRIGGER cohort_analysis_test_universe_immutable_update
        BEFORE UPDATE ON cohort_analysis_test_universe
        BEGIN SELECT RAISE(ABORT, 'cohort analysis test universe is immutable'); END;
        CREATE TRIGGER cohort_analysis_test_universe_immutable_delete
        BEFORE DELETE ON cohort_analysis_test_universe
        BEGIN SELECT RAISE(ABORT, 'cohort analysis test universe is immutable'); END;

        INSERT INTO schema_migrations(version, name, applied_at_utc)
        VALUES (17, 'persist_immutable_cohort_analysis_snapshots',
                strftime('%Y-%m-%dT%H:%M:%fZ', 'now'));
    )sql");
}

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
    if (version < 7) {
        apply_version_seven(connection_);
    }
    if (version < 8) {
        apply_version_eight(connection_);
    }
    if (version < 9) {
        apply_version_nine(connection_);
    }
    if (version < 10) {
        apply_version_ten(connection_);
    }
    if (version < 11) {
        apply_version_eleven(connection_);
    }
    if (version < 12) {
        apply_version_twelve(connection_);
    }
    if (version < 13) {
        apply_version_thirteen(connection_);
    }
    if (version < 14) {
        apply_version_fourteen(connection_);
    }
    if (version < 15) {
        apply_version_fifteen(connection_);
    }
    if (version < 16) {
        apply_version_sixteen(connection_);
    }
    if (version < 17) {
        apply_version_seventeen(connection_);
    }
    transaction.commit();
}

std::int32_t ProjectMigrationRunner::current_version() const {
    return read_current_version(connection_.native_handle());
}

}  // namespace biocore::infrastructure::sqlite
