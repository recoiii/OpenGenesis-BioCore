from pathlib import Path

BASE_SHA = "c7c74402a5385f8974000ff5713c51784454b52f"


def read(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8", newline="\n")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one anchor, found {count}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))


# Domain: attempt number is execution-attempt identity; revision remains concurrency identity.
replace_once(
    "src/domain/include/biocore/domain/job.hpp",
    "        std::int64_t revision,\n        std::optional<JobFailure> failure = std::nullopt\n",
    "        std::int64_t revision,\n        std::optional<JobFailure> failure = std::nullopt,\n        std::int64_t attempt_number = 1\n",
)
replace_once(
    "src/domain/include/biocore/domain/job.hpp",
    "    [[nodiscard]] std::int64_t revision() const noexcept;\n    [[nodiscard]] const std::optional<JobFailure>& failure() const noexcept;\n",
    "    [[nodiscard]] std::int64_t revision() const noexcept;\n    [[nodiscard]] const std::optional<JobFailure>& failure() const noexcept;\n    [[nodiscard]] std::int64_t attempt_number() const noexcept;\n",
)
replace_once(
    "src/domain/include/biocore/domain/job.hpp",
    "    std::int64_t revision_;\n    std::optional<JobFailure> failure_;\n",
    "    std::int64_t revision_;\n    std::optional<JobFailure> failure_;\n    std::int64_t attempt_number_;\n",
)

replace_once(
    "src/domain/source/job.cpp",
    "    const std::int64_t revision,\n    std::optional<JobFailure> failure\n)",
    "    const std::int64_t revision,\n    std::optional<JobFailure> failure,\n    const std::int64_t attempt_number\n)",
)
replace_once(
    "src/domain/source/job.cpp",
    "      revision_{revision},\n      failure_{std::move(failure)} {",
    "      revision_{revision},\n      failure_{std::move(failure)},\n      attempt_number_{attempt_number} {",
)
replace_once(
    "src/domain/source/job.cpp",
    "    if (revision_ < 0) {\n        throw std::invalid_argument(\"Job revision must not be negative\");\n    }\n",
    "    if (revision_ < 0) {\n        throw std::invalid_argument(\"Job revision must not be negative\");\n    }\n    if (attempt_number_ < 1) {\n        throw std::invalid_argument(\"Job attempt number must be at least one\");\n    }\n",
)
replace_once(
    "src/domain/source/job.cpp",
    "const std::optional<JobFailure>& Job::failure() const noexcept {\n    return failure_;\n}\n",
    "const std::optional<JobFailure>& Job::failure() const noexcept {\n    return failure_;\n}\n\nstd::int64_t Job::attempt_number() const noexcept {\n    return attempt_number_;\n}\n",
)
replace_once(
    "src/domain/source/job.cpp",
    "    const bool failure_target = target == JobStatus::failed || target == JobStatus::interrupted;\n    if (!failure_target && failure.has_value()) {\n",
    "    const bool failure_target = target == JobStatus::failed || target == JobStatus::interrupted;\n    const bool retry_transition = status_ == JobStatus::interrupted && target == JobStatus::queued;\n    if (retry_transition && (progress != 0.0 || active_step_id.has_value())) {\n        throw std::invalid_argument(\"Retry transitions must restart from zero progress without an active step\");\n    }\n    if (retry_transition && attempt_number_ == std::numeric_limits<std::int64_t>::max()) {\n        throw std::overflow_error(\"Job attempt number cannot be incremented\");\n    }\n    if (!failure_target && failure.has_value()) {\n",
)
replace_once(
    "src/domain/source/job.cpp",
    "    if (!started_at_utc_.has_value() &&\n        (target == JobStatus::preparing || target == JobStatus::running ||\n         target == JobStatus::paused || target == JobStatus::cancelling)) {\n        started_at_utc_ = transition_at_utc;\n    }\n\n    if (is_terminal(target)) {\n        finished_at_utc_ = transition_at_utc;\n        active_step_id_.reset();\n    } else {\n        finished_at_utc_.reset();\n        active_step_id_ = std::move(active_step_id);\n    }\n",
    "    if (retry_transition) {\n        started_at_utc_.reset();\n        finished_at_utc_.reset();\n        active_step_id_.reset();\n        ++attempt_number_;\n    } else {\n        if (!started_at_utc_.has_value() &&\n            (target == JobStatus::preparing || target == JobStatus::running ||\n             target == JobStatus::paused || target == JobStatus::cancelling)) {\n            started_at_utc_ = transition_at_utc;\n        }\n\n        if (is_terminal(target)) {\n            finished_at_utc_ = transition_at_utc;\n            active_step_id_.reset();\n        } else {\n            finished_at_utc_.reset();\n            active_step_id_ = std::move(active_step_id);\n        }\n    }\n",
)

# Generic JobService transitions must not create a queued job with a stale prepared launch revision.
replace_once(
    "src/application/source/job_service.cpp",
    "    if (!domain::can_transition(job->status(), target)) {\n        throw std::invalid_argument(\"Invalid job status transition\");\n    }\n",
    "    if (job->status() == domain::JobStatus::interrupted && target == domain::JobStatus::queued) {\n        throw std::invalid_argument(\"Interrupted jobs must be retried through JobRetryService\");\n    }\n    if (!domain::can_transition(job->status(), target)) {\n        throw std::invalid_argument(\"Invalid job status transition\");\n    }\n",
)
replace_once(
    "src/application/include/biocore/application/job_service_error.hpp",
    "    identifier_generation_exhausted,\n    job_not_found,\n    concurrent_update\n",
    "    identifier_generation_exhausted,\n    job_not_found,\n    concurrent_update,\n    job_not_retryable,\n    prepared_execution_missing\n",
)

# Prepared execution association keeps immutable snapshot identity while launch revision advances per retry.
replace_once(
    "src/application/include/biocore/application/i_prepared_job_store.hpp",
    "struct PreparedJobExecution final {\n    std::string job_id;\n    std::int64_t launch_revision{0};\n",
    "struct PreparedJobExecution final {\n    std::string job_id;\n    std::int64_t attempt_number{1};\n    std::int64_t launch_revision{0};\n",
)
replace_once(
    "src/application/include/biocore/application/i_prepared_job_store.hpp",
    "    [[nodiscard]] virtual std::optional<PreparedJobExecution> find_execution(\n        std::string_view job_id\n    ) = 0;\n",
    "    // Atomically re-queues one interrupted prepared job and advances only its launch revision.\n    // The immutable execution-plan path, pipeline identity, and original preparation timestamp\n    // must remain unchanged. Returns false on an optimistic-concurrency race.\n    virtual bool retry_prepared_job(\n        const domain::Job& queued_job,\n        std::int64_t expected_revision,\n        const PreparedJobExecution& execution\n    ) = 0;\n\n    [[nodiscard]] virtual std::optional<PreparedJobExecution> find_execution(\n        std::string_view job_id\n    ) = 0;\n",
)
replace_once(
    "src/infrastructure/include/biocore/infrastructure/sqlite/sqlite_prepared_job_store.hpp",
    "    bool add_prepared_job(\n        const domain::Job& queued_job,\n        const application::PreparedJobExecution& execution\n    ) override;\n\n    [[nodiscard]] std::optional<application::PreparedJobExecution> find_execution(\n",
    "    bool add_prepared_job(\n        const domain::Job& queued_job,\n        const application::PreparedJobExecution& execution\n    ) override;\n\n    bool retry_prepared_job(\n        const domain::Job& queued_job,\n        std::int64_t expected_revision,\n        const application::PreparedJobExecution& execution\n    ) override;\n\n    [[nodiscard]] std::optional<application::PreparedJobExecution> find_execution(\n",
)

# Scheduler must match both attempt identity and launch revision.
replace_once(
    "src/application/source/job_scheduler.cpp",
    "    return execution.job_id == job.id() &&\n           execution.launch_revision == expected_launch_revision &&\n",
    "    return execution.job_id == job.id() &&\n           execution.attempt_number == job.attempt_number() &&\n           execution.launch_revision == expected_launch_revision &&\n",
)

# SQLite job persistence: persist attempt_number as a distinct durable field.
path = "src/infrastructure/source/sqlite/sqlite_job_repository.cpp"
text = read(path)
text = text.replace("if (statement.is_null(13)) {", "if (statement.is_null(14)) {", 1)
text = text.replace("if (!statement.is_null(14) || !statement.is_null(15) || !statement.is_null(16) ||\n            !statement.is_null(17))", "if (!statement.is_null(15) || !statement.is_null(16) || !statement.is_null(17) ||\n            !statement.is_null(18))", 1)
text = text.replace("if (statement.is_null(14) || statement.is_null(17))", "if (statement.is_null(15) || statement.is_null(18))", 1)
text = text.replace("statement.text(13)", "statement.text(14)", 1)
text = text.replace("statement.text(14),\n            statement.is_null(15) ? std::nullopt\n                                  : std::optional<std::int64_t>{statement.integer(15)},\n            statement.optional_text(16),\n            statement.text(17),", "statement.text(15),\n            statement.is_null(16) ? std::nullopt\n                                  : std::optional<std::int64_t>{statement.integer(16)},\n            statement.optional_text(17),\n            statement.text(18),", 1)
text = text.replace("            statement.integer(12),\n            read_failure(statement),\n", "            statement.integer(12),\n            read_failure(statement),\n            statement.integer(13),\n", 1)
text = text.replace("    revision,\n    failure_kind,", "    revision,\n    attempt_number,\n    failure_kind,", 1)
text = text.replace("            finished_at_utc, revision, failure_kind, failure_message, failure_exit_code,\n            failure_worker_timestamp_utc, failure_recorded_at_utc\n        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", "            finished_at_utc, revision, attempt_number, failure_kind, failure_message, failure_exit_code,\n            failure_worker_timestamp_utc, failure_recorded_at_utc\n        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", 1)
text = text.replace("    statement.bind_integer(13, job.revision());\n    if (job.failure().has_value()) {\n        statement.bind_text(14,", "    statement.bind_integer(13, job.revision());\n    statement.bind_integer(14, job.attempt_number());\n    if (job.failure().has_value()) {\n        statement.bind_text(15,", 1)
text = text.replace("        statement.bind_text(15, job.failure()->message());\n        if (job.failure()->exit_code().has_value()) {\n            statement.bind_integer(16, *job.failure()->exit_code());\n        } else {\n            statement.bind_null(16);\n        }\n        statement.bind_optional_text(17, job.failure()->worker_timestamp_utc());\n        statement.bind_text(18, job.failure()->recorded_at_utc());\n    } else {\n        statement.bind_null(14);\n        statement.bind_null(15);\n        statement.bind_null(16);\n        statement.bind_null(17);\n        statement.bind_null(18);\n", "        statement.bind_text(16, job.failure()->message());\n        if (job.failure()->exit_code().has_value()) {\n            statement.bind_integer(17, *job.failure()->exit_code());\n        } else {\n            statement.bind_null(17);\n        }\n        statement.bind_optional_text(18, job.failure()->worker_timestamp_utc());\n        statement.bind_text(19, job.failure()->recorded_at_utc());\n    } else {\n        statement.bind_null(15);\n        statement.bind_null(16);\n        statement.bind_null(17);\n        statement.bind_null(18);\n        statement.bind_null(19);\n", 1)
text = text.replace("            revision = ?,\n            failure_kind = ?,", "            revision = ?,\n            attempt_number = ?,\n            failure_kind = ? ,", 1)
# Remove the harmless spacing introduced above so exact SQL remains conventional.
text = text.replace("failure_kind = ? ,", "failure_kind = ?,", 1)
text = text.replace("    statement.bind_integer(7, job.revision());\n    if (job.failure().has_value()) {\n        statement.bind_text(8,", "    statement.bind_integer(7, job.revision());\n    statement.bind_integer(8, job.attempt_number());\n    if (job.failure().has_value()) {\n        statement.bind_text(9,", 1)
text = text.replace("        statement.bind_text(9, job.failure()->message());\n        if (job.failure()->exit_code().has_value()) {\n            statement.bind_integer(10, *job.failure()->exit_code());\n        } else {\n            statement.bind_null(10);\n        }\n        statement.bind_optional_text(11, job.failure()->worker_timestamp_utc());\n        statement.bind_text(12, job.failure()->recorded_at_utc());\n    } else {\n        statement.bind_null(8);\n        statement.bind_null(9);\n        statement.bind_null(10);\n        statement.bind_null(11);\n        statement.bind_null(12);\n    }\n    statement.bind_text(13, job.id());\n    statement.bind_integer(14, expected_revision);", "        statement.bind_text(10, job.failure()->message());\n        if (job.failure()->exit_code().has_value()) {\n            statement.bind_integer(11, *job.failure()->exit_code());\n        } else {\n            statement.bind_null(11);\n        }\n        statement.bind_optional_text(12, job.failure()->worker_timestamp_utc());\n        statement.bind_text(13, job.failure()->recorded_at_utc());\n    } else {\n        statement.bind_null(9);\n        statement.bind_null(10);\n        statement.bind_null(11);\n        statement.bind_null(12);\n        statement.bind_null(13);\n    }\n    statement.bind_text(14, job.id());\n    statement.bind_integer(15, expected_revision);", 1)
write(path, text)

# SQLite prepared store: initial attempt=1; retry transaction advances job + launch revision atomically.
replace_once(
    "src/infrastructure/source/sqlite/sqlite_prepared_job_store.cpp",
    "#include <cstdint>\n#include <optional>",
    "#include <cstdint>\n#include <limits>\n#include <optional>",
)
replace_once(
    "src/infrastructure/source/sqlite/sqlite_prepared_job_store.cpp",
    "    if (job.status() != domain::JobStatus::queued || job.revision() <= 0 ||\n        execution.job_id != job.id() || execution.launch_revision != job.revision() + 1 ||\n",
    "    if (job.status() != domain::JobStatus::queued || job.revision() <= 0 ||\n        job.attempt_number() != 1 || execution.attempt_number != 1 ||\n        execution.job_id != job.id() || execution.launch_revision != job.revision() + 1 ||\n",
)
replace_once(
    "src/infrastructure/source/sqlite/sqlite_prepared_job_store.cpp",
    "}\n\n}  // namespace\n",
    "}\n\nvoid validate_retry(\n    const domain::Job& job,\n    const std::int64_t expected_revision,\n    const application::PreparedJobExecution& execution\n) {\n    if (expected_revision < 0 || job.status() != domain::JobStatus::queued ||\n        job.revision() != expected_revision + 1 || job.attempt_number() <= 1 ||\n        execution.attempt_number != job.attempt_number() || execution.job_id != job.id() ||\n        job.revision() == std::numeric_limits<std::int64_t>::max() ||\n        execution.launch_revision != job.revision() + 1 ||\n        !job.pipeline_id().has_value() || !job.pipeline_version().has_value() ||\n        execution.pipeline_id != *job.pipeline_id() ||\n        execution.pipeline_version != *job.pipeline_version() ||\n        execution.execution_plan_path.empty() || execution.prepared_at_utc.empty()) {\n        throw std::invalid_argument(\"Retry execution does not match the queued Job\");\n    }\n}\n\n}  // namespace\n",
)
replace_once(
    "src/infrastructure/source/sqlite/sqlite_prepared_job_store.cpp",
    "            active_step_id, created_at_utc, updated_at_utc, started_at_utc,\n            finished_at_utc, revision\n        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
    "            active_step_id, created_at_utc, updated_at_utc, started_at_utc,\n            finished_at_utc, revision, attempt_number\n        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
)
replace_once(
    "src/infrastructure/source/sqlite/sqlite_prepared_job_store.cpp",
    "    job_statement.bind_integer(13, job.revision());\n    require_done",
    "    job_statement.bind_integer(13, job.revision());\n    job_statement.bind_integer(14, job.attempt_number());\n    require_done",
)
replace_once(
    "src/infrastructure/source/sqlite/sqlite_prepared_job_store.cpp",
    "std::optional<application::PreparedJobExecution> SqlitePreparedJobStore::find_execution(\n",
    "bool SqlitePreparedJobStore::retry_prepared_job(\n    const domain::Job& job,\n    const std::int64_t expected_revision,\n    const application::PreparedJobExecution& execution\n) {\n    validate_retry(job, expected_revision, execution);\n    sqlite3* database = connection_.native_handle();\n    Transaction transaction{connection_};\n\n    constexpr const char* update_job = R\"sql(\n        UPDATE jobs SET\n            status = ?, progress = ?, active_step_id = ?, updated_at_utc = ?,\n            started_at_utc = ?, finished_at_utc = ?, revision = ?, attempt_number = ?,\n            failure_kind = NULL, failure_message = NULL, failure_exit_code = NULL,\n            failure_worker_timestamp_utc = NULL, failure_recorded_at_utc = NULL\n        WHERE id = ? AND revision = ? AND status = 'interrupted';\n    )sql\";\n    Statement job_statement{database, update_job};\n    job_statement.bind_text(1, domain::to_string(job.status()));\n    job_statement.bind_double(2, job.progress());\n    job_statement.bind_optional_text(3, job.active_step_id());\n    job_statement.bind_text(4, job.updated_at_utc());\n    job_statement.bind_optional_text(5, job.started_at_utc());\n    job_statement.bind_optional_text(6, job.finished_at_utc());\n    job_statement.bind_integer(7, job.revision());\n    job_statement.bind_integer(8, job.attempt_number());\n    job_statement.bind_text(9, job.id());\n    job_statement.bind_integer(10, expected_revision);\n    require_done(database, job_statement, \"Unable to persist retried job\");\n    if (sqlite3_changes(database) != 1) {\n        return false;\n    }\n\n    constexpr const char* update_execution = R\"sql(\n        UPDATE job_execution_plans SET launch_revision = ? WHERE job_id = ?;\n    )sql\";\n    Statement execution_statement{database, update_execution};\n    execution_statement.bind_integer(1, execution.launch_revision);\n    execution_statement.bind_text(2, execution.job_id);\n    require_done(database, execution_statement, \"Unable to advance retry launch revision\");\n    if (sqlite3_changes(database) != 1) {\n        throw SqliteError{SQLITE_CONSTRAINT, \"Prepared execution association is missing during retry\"};\n    }\n\n    transaction.commit();\n    return true;\n}\n\nstd::optional<application::PreparedJobExecution> SqlitePreparedJobStore::find_execution(\n",
)
replace_once(
    "src/infrastructure/source/sqlite/sqlite_prepared_job_store.cpp",
    "        SELECT job_id, launch_revision, pipeline_id, pipeline_version,\n               execution_plan_path, prepared_at_utc\n        FROM job_execution_plans WHERE job_id = ?;\n",
    "        SELECT p.job_id, j.attempt_number, p.launch_revision, p.pipeline_id, p.pipeline_version,\n               p.execution_plan_path, p.prepared_at_utc\n        FROM job_execution_plans AS p\n        JOIN jobs AS j ON j.id = p.job_id\n        WHERE p.job_id = ?;\n",
)
replace_once(
    "src/infrastructure/source/sqlite/sqlite_prepared_job_store.cpp",
    "        .job_id = statement.text(0),\n        .launch_revision = statement.integer(1),\n        .pipeline_id = statement.text(2),\n        .pipeline_version = statement.text(3),\n        .execution_plan_path = statement.text(4),\n        .prepared_at_utc = statement.text(5),\n",
    "        .job_id = statement.text(0),\n        .attempt_number = statement.integer(1),\n        .launch_revision = statement.integer(2),\n        .pipeline_id = statement.text(3),\n        .pipeline_version = statement.text(4),\n        .execution_plan_path = statement.text(5),\n        .prepared_at_utc = statement.text(6),\n",
)

# Retry service.
write("src/application/include/biocore/application/job_retry_service.hpp", r'''#pragma once

#include <string_view>

#include "biocore/application/i_prepared_job_store.hpp"
#include "biocore/domain/job.hpp"

namespace biocore::application {

class JobService;
class IUtcClock;

struct RetryJobResult final {
    domain::Job job;
    PreparedJobExecution execution;
};

class JobRetryService final {
public:
    JobRetryService(
        JobService& jobs,
        IPreparedJobStore& prepared_jobs,
        IUtcClock& clock
    ) noexcept;

    [[nodiscard]] RetryJobResult retry(std::string_view job_id);

private:
    JobService& jobs_;
    IPreparedJobStore& prepared_jobs_;
    IUtcClock& clock_;
};

}  // namespace biocore::application
''')
write("src/application/source/job_retry_service.cpp", r'''#include "biocore/application/job_retry_service.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/job_service_error.hpp"
#include "biocore/domain/job_status.hpp"

namespace biocore::application {

JobRetryService::JobRetryService(
    JobService& jobs,
    IPreparedJobStore& prepared_jobs,
    IUtcClock& clock
) noexcept
    : jobs_{jobs}, prepared_jobs_{prepared_jobs}, clock_{clock} {}

RetryJobResult JobRetryService::retry(const std::string_view job_id) {
    auto job = jobs_.find_by_id(job_id);
    if (!job.has_value()) {
        throw JobServiceError{JobServiceErrorCode::job_not_found, "Job was not found"};
    }
    if (job->status() != domain::JobStatus::interrupted) {
        throw JobServiceError{
            JobServiceErrorCode::job_not_retryable,
            "Only interrupted jobs may be retried without changing their immutable execution plan",
        };
    }

    const auto prepared = prepared_jobs_.find_execution(job_id);
    if (!prepared.has_value()) {
        throw JobServiceError{
            JobServiceErrorCode::prepared_execution_missing,
            "Interrupted job has no prepared execution-plan association",
        };
    }
    if (job->revision() >= std::numeric_limits<std::int64_t>::max() - 1) {
        throw std::overflow_error("Job revision cannot reserve a retry launch revision");
    }

    const std::int64_t expected_revision = job->revision();
    job->transition_to(
        domain::JobStatus::queued,
        0.0,
        std::nullopt,
        clock_.now_utc_iso8601()
    );

    PreparedJobExecution retry_execution = *prepared;
    retry_execution.attempt_number = job->attempt_number();
    retry_execution.launch_revision = job->revision() + 1;

    if (!prepared_jobs_.retry_prepared_job(*job, expected_revision, retry_execution)) {
        throw JobServiceError{
            JobServiceErrorCode::concurrent_update,
            "Job changed before its retry could be persisted",
        };
    }

    return RetryJobResult{.job = *job, .execution = std::move(retry_execution)};
}

}  // namespace biocore::application
''')

replace_once(
    "src/application/CMakeLists.txt",
    "        source/job_scheduler_error.cpp\n        source/job_service.cpp\n",
    "        source/job_scheduler_error.cpp\n        source/job_retry_service.cpp\n        source/job_service.cpp\n",
)
replace_once(
    "src/application/CMakeLists.txt",
    "            include/biocore/application/job_scheduler_error.hpp\n            include/biocore/application/job_service.hpp\n",
    "            include/biocore/application/job_scheduler_error.hpp\n            include/biocore/application/job_retry_service.hpp\n            include/biocore/application/job_service.hpp\n",
)

# Schema v8: attempt identity and immutable-plan retry guardrails.
replace_once(
    "src/infrastructure/include/biocore/infrastructure/sqlite/project_migration_runner.hpp",
    "inline constexpr std::int32_t latest_project_schema_version = 7;",
    "inline constexpr std::int32_t latest_project_schema_version = 8;",
)
replace_once(
    "src/infrastructure/source/sqlite/project_migration_runner.cpp",
    "}\n\n}  // namespace\n\nProjectMigrationRunner::ProjectMigrationRunner",
    r''' }

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

ProjectMigrationRunner::ProjectMigrationRunner''',
)
replace_once(
    "src/infrastructure/source/sqlite/project_migration_runner.cpp",
    "    if (version < 7) {\n        apply_version_seven(connection_);\n    }\n    transaction.commit();",
    "    if (version < 7) {\n        apply_version_seven(connection_);\n    }\n    if (version < 8) {\n        apply_version_eight(connection_);\n    }\n    transaction.commit();",
)

# Project database compatibility guard knows schema v8 and its retry guard triggers.
replace_once(
    "src/infrastructure/source/sqlite/project_database_guard.cpp",
    "constexpr std::array<ExpectedMigration, 7> expected_migrations{{",
    "constexpr std::array<ExpectedMigration, 8> expected_migrations{{",
)
replace_once(
    "src/infrastructure/source/sqlite/project_database_guard.cpp",
    "    {7, \"persist_structured_job_failure_evidence\"},\n}};",
    "    {7, \"persist_structured_job_failure_evidence\"},\n    {8, \"add_explicit_retry_attempt_semantics\"},\n}};",
)
replace_once(
    "src/infrastructure/source/sqlite/project_database_guard.cpp",
    "constexpr std::array<RequiredSchemaObject, 13> required_current_objects{{",
    "constexpr std::array<RequiredSchemaObject, 16> required_current_objects{{",
)
replace_once(
    "src/infrastructure/source/sqlite/project_database_guard.cpp",
    "    {\"trigger\", \"jobs_validate_failure_evidence_update\"},\n}};",
    "    {\"trigger\", \"jobs_validate_failure_evidence_update\"},\n    {\"trigger\", \"jobs_validate_attempt_update\"},\n    {\"trigger\", \"job_execution_plans_immutable_fields_update\"},\n    {\"trigger\", \"job_execution_plans_launch_revision_monotonic\"},\n}};",
)

# API exposes attemptNumber and an explicit retry operation; constructor remains backward-compatible for tests.
replace_once(
    "src/presentation/include/biocore/presentation/local_api.hpp",
    "class JobService;\nclass ManagedFileService;",
    "class JobService;\nclass JobRetryService;\nclass ManagedFileService;",
)
replace_once(
    "src/presentation/include/biocore/presentation/local_api.hpp",
    "        std::string bootstrap_token,\n        LocalBrowserSession& browser_session\n    );",
    "        std::string bootstrap_token,\n        LocalBrowserSession& browser_session,\n        application::JobRetryService* retries = nullptr\n    );",
)
replace_once(
    "src/presentation/include/biocore/presentation/local_api.hpp",
    "    application::JobService& jobs_;\n    application::IJobSubmitter& submissions_;",
    "    application::JobService& jobs_;\n    application::JobRetryService* retries_;\n    application::IJobSubmitter& submissions_;",
)
replace_once(
    "src/presentation/source/local_api.cpp",
    "#include \"biocore/application/job_service.hpp\"\n#include \"biocore/application/job_service_error.hpp\"",
    "#include \"biocore/application/job_retry_service.hpp\"\n#include \"biocore/application/job_service.hpp\"\n#include \"biocore/application/job_service_error.hpp\"",
)
replace_once(
    "src/presentation/source/local_api.cpp",
    "           \",\\\"revision\\\":\" + std::to_string(job.revision()) +\n           \",\\\"failure\\\":\" + render_failure(job.failure()) + \"}\";",
    "           \",\\\"revision\\\":\" + std::to_string(job.revision()) +\n           \",\\\"attemptNumber\\\":\" + std::to_string(job.attempt_number()) +\n           \",\\\"failure\\\":\" + render_failure(job.failure()) + \"}\";",
)
replace_once(
    "src/presentation/source/local_api.cpp",
    "    application::ArtifactPresentationService& artifacts,\n    application::IUtcClock& clock,\n    std::string bootstrap_token,\n    LocalBrowserSession& browser_session\n)\n    : jobs_{jobs}, submissions_{submissions}, managed_files_{managed_files}, artifacts_{artifacts}, clock_{clock},\n",
    "    application::ArtifactPresentationService& artifacts,\n    application::IUtcClock& clock,\n    std::string bootstrap_token,\n    LocalBrowserSession& browser_session,\n    application::JobRetryService* retries\n)\n    : jobs_{jobs}, retries_{retries}, submissions_{submissions}, managed_files_{managed_files}, artifacts_{artifacts}, clock_{clock},\n",
)
replace_once(
    "src/presentation/source/local_api.cpp",
    "            if (path.size() == 5U && path[4] == \"cancel\" &&\n                request.method == HttpMethod::post) {",
    "            if (path.size() == 5U && path[4] == \"retry\" &&\n                request.method == HttpMethod::post) {\n                if (!request.body.empty()) {\n                    throw std::invalid_argument(\"Job retry request body must be empty\");\n                }\n                if (retries_ == nullptr) {\n                    return error_response(503, \"job_retry_unavailable\", \"Job retry service is unavailable\");\n                }\n                const auto retried = retries_->retry(job_id);\n                return json_response(202, render_job(retried.job));\n            }\n            if (path.size() == 5U && path[4] == \"cancel\" &&\n                request.method == HttpMethod::post) {",
)
replace_once(
    "src/presentation/source/local_api.cpp",
    "        if (error.code() == application::JobServiceErrorCode::concurrent_update) {\n            return error_response(409, \"concurrent_update\", error.what());\n        }\n        return error_response(500, \"job_service_error\", error.what());",
    "        if (error.code() == application::JobServiceErrorCode::concurrent_update) {\n            return error_response(409, \"concurrent_update\", error.what());\n        }\n        if (error.code() == application::JobServiceErrorCode::job_not_retryable) {\n            return error_response(409, \"job_not_retryable\", error.what());\n        }\n        if (error.code() == application::JobServiceErrorCode::prepared_execution_missing) {\n            return error_response(409, \"job_retry_unavailable\", error.what());\n        }\n        return error_response(500, \"job_service_error\", error.what());",
)

# Composition root wires retry service on the API-owned SQLite connection.
replace_once(
    "apps/biocore/local_server_bootstrap.cpp",
    "#include \"biocore/application/job_scheduler.hpp\"\n#include \"biocore/application/job_service.hpp\"",
    "#include \"biocore/application/job_scheduler.hpp\"\n#include \"biocore/application/job_retry_service.hpp\"\n#include \"biocore/application/job_service.hpp\"",
)
replace_once(
    "apps/biocore/local_server_bootstrap.cpp",
    "    application::JobSubmissionService submissions{api_prepared_jobs, pipelines, preparation, execution_plans, api_ids, clock};\n\n    infrastructure::PlatformWorkerSupervisor",
    "    application::JobSubmissionService submissions{api_prepared_jobs, pipelines, preparation, execution_plans, api_ids, clock};\n    application::JobRetryService retries{api_jobs, api_prepared_jobs, clock};\n\n    infrastructure::PlatformWorkerSupervisor",
)
replace_once(
    "apps/biocore/local_server_bootstrap.cpp",
    "        api_jobs, submissions, managed_files, artifacts, clock, token, browser_session\n    };",
    "        api_jobs, submissions, managed_files, artifacts, clock, token, browser_session, &retries\n    };",
)

# Preserve 047 regression intent: generic requeue is now rejected; dedicated retry service owns clearing.
replace_once(
    "tests/job_failure_diagnostics_tests.cpp",
    "    job = jobs.transition(job.id(), domain::JobStatus::queued, 0.2, std::nullopt);\n    const auto stored = repository.find_by_id(job.id());\n    return stored.has_value() && stored->status() == domain::JobStatus::queued &&\n           !stored->failure().has_value();",
    "    try {\n        static_cast<void>(jobs.transition(job.id(), domain::JobStatus::queued, 0.0, std::nullopt));\n        return false;\n    } catch (const std::invalid_argument&) {\n    }\n    const auto stored = repository.find_by_id(job.id());\n    return stored.has_value() && stored->status() == domain::JobStatus::interrupted &&\n           stored->failure().has_value();",
)
replace_once(
    "tests/job_failure_diagnostics_tests.cpp",
    "        INSERT INTO jobs(\n            id, status, progress, created_at_utc, updated_at_utc, started_at_utc,\n            finished_at_utc, revision\n        ) VALUES\n            ('legacy-failed', 'failed', 0.4, 'c', 'u', 's', 'f', 4),\n            ('legacy-interrupted', 'interrupted', 0.7, 'c2', 'u2', 's2', NULL, 7);\n",
    "        INSERT INTO jobs(\n            id, status, progress, created_at_utc, updated_at_utc, started_at_utc,\n            finished_at_utc, revision\n        ) VALUES\n            ('legacy-failed', 'failed', 0.4, 'c', 'u', 's', 'f', 4),\n            ('legacy-interrupted', 'interrupted', 0.7, 'c2', 'u2', 's2', NULL, 7);\n\n        CREATE TABLE job_execution_plans (\n            job_id TEXT PRIMARY KEY NOT NULL REFERENCES jobs(id) ON DELETE CASCADE,\n            launch_revision INTEGER NOT NULL,\n            pipeline_id TEXT NOT NULL,\n            pipeline_version TEXT NOT NULL,\n            execution_plan_path TEXT NOT NULL UNIQUE,\n            prepared_at_utc TEXT NOT NULL\n        );\n",
)
replace_once(
    "tests/job_failure_diagnostics_tests.cpp",
    "    if (migrations.current_version() != 7) return false;",
    "    if (migrations.current_version() != 8) return false;",
)

# New end-to-end retry semantics CTest.
write("tests/job_retry_semantics_tests.cpp", r'''#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_retry_service.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/job_service_error.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/job_failure.hpp"
#include "biocore/domain/job_status.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"
#include "biocore/infrastructure/sqlite/sqlite_job_repository.hpp"
#include "biocore/infrastructure/sqlite/sqlite_prepared_job_store.hpp"

namespace {

using namespace biocore;

class UnusedIdGenerator final : public application::IIdGenerator {
public:
    std::string generate() override { throw std::runtime_error("unused"); }
};

class SequenceClock final : public application::IUtcClock {
public:
    explicit SequenceClock(std::deque<std::string> values) : values_{std::move(values)} {}
    std::string now_utc_iso8601() override {
        if (values_.empty()) throw std::runtime_error("clock exhausted");
        auto value = std::move(values_.front());
        values_.pop_front();
        return value;
    }
private:
    std::deque<std::string> values_;
};

[[nodiscard]] bool statement_throws(
    infrastructure::sqlite::SqliteConnection& connection,
    const std::string& sql
) {
    try {
        connection.execute(sql);
    } catch (const infrastructure::sqlite::SqliteError&) {
        return true;
    }
    return false;
}

[[nodiscard]] bool retry_contract() {
    infrastructure::sqlite::SqliteConnection connection{std::filesystem::path{":memory:"}};
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    if (migrations.current_version() != 8) return false;

    infrastructure::sqlite::SqliteJobRepository repository{connection};
    infrastructure::sqlite::SqlitePreparedJobStore prepared{connection};

    domain::Job queued{
        "retry-job",
        std::string{"analysis-retry"},
        std::string{"org.biocore.retry"},
        std::string{"1.0.0"},
        domain::JobStatus::queued,
        domain::JobPriority::normal,
        0.0,
        std::nullopt,
        "created",
        "queued",
        std::nullopt,
        std::nullopt,
        1,
    };
    const application::PreparedJobExecution initial_execution{
        .job_id = "retry-job",
        .attempt_number = 1,
        .launch_revision = 2,
        .pipeline_id = "org.biocore.retry",
        .pipeline_version = "1.0.0",
        .execution_plan_path = "plans/retry-job.json",
        .prepared_at_utc = "queued",
    };
    if (!prepared.add_prepared_job(queued, initial_execution)) return false;

    UnusedIdGenerator ids;
    SequenceClock clock{{
        "prepare-1", "run-1", "interrupt-1", "retry-1",
        "prepare-2", "run-2", "interrupt-2", "retry-2"
    }};
    application::JobService jobs{repository, ids, clock};
    application::JobRetryService retries{jobs, prepared, clock};

    auto job = jobs.transition("retry-job", domain::JobStatus::preparing, 0.0, std::nullopt);
    job = jobs.transition("retry-job", domain::JobStatus::running, 0.1, std::string{"step-a"});
    job = jobs.transition(
        "retry-job",
        domain::JobStatus::interrupted,
        0.3,
        std::nullopt,
        application::JobFailureContext{
            .kind = domain::JobFailureKind::process_exit_without_terminal,
            .message = "worker exited before terminal event",
            .exit_code = 9,
            .worker_timestamp_utc = std::nullopt,
        }
    );
    if (job.revision() != 4 || job.attempt_number() != 1 || !job.failure().has_value()) return false;

    try {
        static_cast<void>(jobs.transition(
            "retry-job", domain::JobStatus::queued, 0.0, std::nullopt
        ));
        return false;
    } catch (const std::invalid_argument&) {
    }

    const auto first_retry = retries.retry("retry-job");
    if (first_retry.job.status() != domain::JobStatus::queued ||
        first_retry.job.progress() != 0.0 || first_retry.job.failure().has_value() ||
        first_retry.job.started_at_utc().has_value() || first_retry.job.finished_at_utc().has_value() ||
        first_retry.job.attempt_number() != 2 || first_retry.job.revision() != 5 ||
        first_retry.execution.attempt_number != 2 || first_retry.execution.launch_revision != 6 ||
        first_retry.execution.execution_plan_path != "plans/retry-job.json" ||
        first_retry.execution.prepared_at_utc != "queued") {
        return false;
    }

    const auto stored_execution = prepared.find_execution("retry-job");
    if (!stored_execution.has_value() || stored_execution->attempt_number != 2 ||
        stored_execution->launch_revision != 6 ||
        stored_execution->execution_plan_path != "plans/retry-job.json") {
        return false;
    }

    if (!statement_throws(
            connection,
            "UPDATE job_execution_plans SET execution_plan_path = 'plans/mutated.json' "
            "WHERE job_id = 'retry-job';"
        ) ||
        !statement_throws(
            connection,
            "UPDATE job_execution_plans SET launch_revision = 5 WHERE job_id = 'retry-job';"
        )) {
        return false;
    }

    job = jobs.transition("retry-job", domain::JobStatus::preparing, 0.0, std::nullopt);
    if (job.revision() != 6 || job.attempt_number() != 2) return false;
    job = jobs.transition("retry-job", domain::JobStatus::running, 0.2, std::string{"step-a"});
    job = jobs.transition(
        "retry-job",
        domain::JobStatus::interrupted,
        0.5,
        std::nullopt,
        application::JobFailureContext{
            .kind = domain::JobFailureKind::heartbeat_timeout,
            .message = "heartbeat timeout",
            .exit_code = std::nullopt,
            .worker_timestamp_utc = std::nullopt,
        }
    );
    const auto second_retry = retries.retry("retry-job");
    if (second_retry.job.attempt_number() != 3 || second_retry.job.revision() != 9 ||
        second_retry.execution.attempt_number != 3 || second_retry.execution.launch_revision != 10 ||
        second_retry.execution.execution_plan_path != "plans/retry-job.json") {
        return false;
    }

    try {
        static_cast<void>(retries.retry("retry-job"));
        return false;
    } catch (const application::JobServiceError& error) {
        return error.code() == application::JobServiceErrorCode::job_not_retryable;
    }
}

}  // namespace

int main() {
    if (!retry_contract()) {
        std::cerr << "Job retry/recovery semantics contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
''')

replace_once(
    "CMakeLists.txt",
    "    add_test(\n        NAME integration.job_failure_diagnostics\n        COMMAND biocore-job-failure-diagnostics-tests\n    )\nendif()",
    "    add_test(\n        NAME integration.job_failure_diagnostics\n        COMMAND biocore-job-failure-diagnostics-tests\n    )\n\n    add_executable(\n        biocore-job-retry-semantics-tests\n        tests/job_retry_semantics_tests.cpp\n    )\n    target_link_libraries(\n        biocore-job-retry-semantics-tests\n        PRIVATE BioCore::infrastructure BioCore::project_warnings BioCore::sanitizers\n    )\n    add_test(\n        NAME integration.job_retry_semantics\n        COMMAND biocore-job-retry-semantics-tests\n    )\nendif()",
)

# CI iteration/test floor.
replace_once(
    ".github/workflows/v0.2-validation.yml",
    "  BIOCORE_ITERATION: \"047\"\n  BIOCORE_TEST_FLOOR: \"69\"",
    "  BIOCORE_ITERATION: \"048\"\n  BIOCORE_TEST_FLOOR: \"70\"",
)

# Acceptance record for frozen 047.
write("docs/development/ITERATION-047-ACCEPTANCE.md", '''# Iteration 047 Acceptance Record

## Status

ACCEPTED & FROZEN

## Exact candidate

- Commit: `c7c74402a5385f8974000ff5713c51784454b52f`
- Frozen reference: `accepted/iteration-047`
- GitHub Actions run: `33493035846`
- Gemini review artifact: `OpenGenesis-BioCore-iteration-047-GEMINI-review`
- Artifact digest: `sha256:05189055e373c9e7333848ec71a996e6419f8302b6da19b8462686015d789b4c`

## Validation

- GCC Debug: 69/69 PASS
- GCC Release: 69/69 PASS
- Clang Debug: 69/69 PASS
- GCC ASan+UBSan: 69/69 PASS
- Total Linux matrix: 276/276 PASS
- `biocore --version`: `0.2.0-dev`
- Browser durable failure-evidence contract: PASS

## Independent review

Gemini verdict: `ACCEPT` with 100% confidence.

Blocking findings: NONE.

Non-blocking findings: NONE.

Iteration 047 is immutable at the exact commit above. Subsequent acceptance records or development work belong to later commits and must not move the frozen reference.
''')

write("docs/development/ITERATION-048.md", '''# OpenGenesis-BioCore v0.2.0-dev — Iteration 048

## Title

Retry & Recovery Semantics

## Goal

Make interrupted-job retry explicit, atomic, durable, and auditable without changing the immutable execution-plan snapshot that was accepted at original submission time.

Iteration 048 advances the project database to schema v8. Worker Protocol remains v2.

## Intended changes

- add a durable `attempt_number` to jobs, starting at 1;
- keep `revision` as optimistic-concurrency/runtime revision rather than overloading it as a retry counter;
- allow retry only for `interrupted` jobs; `failed`, `cancelled`, and `completed` jobs remain non-retryable in this iteration;
- reset retry progress to 0, clear active/start/finish runtime state, and clear prior interruption failure evidence;
- atomically persist interrupted→queued, attempt advancement, and the next scheduler launch revision in one SQLite transaction;
- reuse the exact immutable execution-plan snapshot path and pipeline identity from the original prepared association;
- prohibit updates to execution-plan path, pipeline id/version, or original preparation timestamp with schema-v8 triggers;
- require launch revisions to advance monotonically;
- prevent generic `JobService` interrupted→queued transitions so a queued retry cannot be persisted with a stale prepared launch revision;
- expose `attemptNumber` in job JSON and add `POST /api/v1/jobs/{id}/retry` for the local API;
- preserve explicit recovery: startup recovery marks stale work interrupted but never auto-retries it;
- add a dedicated retry semantics CTest and raise the active CI floor from 69 to 70.

## Explicit non-goals

Iteration 048 must not:

- mutate or regenerate the original execution-plan snapshot during retry;
- retry `failed` jobs;
- automatically retry after process failure, heartbeat timeout, or startup recovery;
- change Worker Protocol version 2;
- change plugin or pipeline identifiers/versions;
- change biological algorithms, thresholds, or scientific outputs;
- change loopback-only networking, browser-session isolation, token separation, or process-tree cancellation ownership;
- add retry backoff policies, retry limits, or distributed/cloud scheduling.

## Acceptance criteria

1. A newly submitted prepared job has `attemptNumber == 1`.
2. Only an `interrupted` prepared job can be retried.
3. A retry increments `attemptNumber` exactly once while `revision` continues its independent monotonic runtime sequence.
4. Retry resets progress to 0 and clears active step, started/finished timestamps, and prior interruption failure evidence.
5. Job retry state and next `launch_revision` are persisted atomically; a concurrency race leaves both unchanged.
6. The execution-plan path, pipeline id/version, and original preparation timestamp are byte-for-byte unchanged across retries.
7. SQLite rejects mutation of immutable prepared-plan identity and non-monotonic launch revisions.
8. The scheduler validates both attempt identity and expected launch revision before worker launch.
9. Generic `JobService` interrupted→queued transition is rejected; retry must use `JobRetryService`.
10. `POST /api/v1/jobs/{id}/retry` returns the newly queued job and job JSON exposes `attemptNumber`.
11. Startup recovery remains interruption-only and never auto-requeues work.
12. Active CTest floor is at least 70 and GCC Debug, GCC Release, Clang Debug, and GCC ASan+UBSan all pass.
13. `0.2.0-dev`, Worker Protocol v2, security boundaries, process-tree cancellation, and biological behavior remain unchanged.
14. Independent Gemini review returns `ACCEPT` before Iteration 048 is frozen.

## Freeze rule

Iteration 048 remains open until the exact final candidate passes the complete Linux validation matrix and independent Gemini review. Any blocking finding requires a revised Iteration 048 candidate and a fresh exact four-part review package.
''')

# Ensure the staging helpers never enter the final candidate tree.
Path("scripts/iteration-048-apply.py").unlink(missing_ok=True)
Path(".github/workflows/iteration-048-apply.yml").unlink(missing_ok=True)
print("Iteration 048 transformations applied")
