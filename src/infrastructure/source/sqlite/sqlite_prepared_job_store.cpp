#include "biocore/infrastructure/sqlite/sqlite_prepared_job_store.hpp"

#include <sqlite3.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/job_status.hpp"
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
            try { connection_.execute("ROLLBACK;"); } catch (...) {}
        }
    }
    void commit() { connection_.execute("COMMIT;"); committed_ = true; }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
private:
    SqliteConnection& connection_;
    bool committed_{false};
};

class Statement final {
public:
    Statement(sqlite3* database, const char* sql) : database_{database} {
        const int result = sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr);
        if (result != SQLITE_OK) {
            throw SqliteError{result, std::string{"Unable to prepare prepared-job statement: "} + sqlite3_errmsg(database_)};
        }
    }
    ~Statement() { if (statement_ != nullptr) sqlite3_finalize(statement_); }
    void bind_text(int index, std::string_view value) {
        require(sqlite3_bind_text64(statement_, index, value.data(), static_cast<sqlite3_uint64>(value.size()), SQLITE_TRANSIENT, SQLITE_UTF8));
    }
    void bind_optional_text(int index, const std::optional<std::string>& value) {
        if (value.has_value()) bind_text(index, *value); else require(sqlite3_bind_null(statement_, index));
    }
    void bind_double(int index, double value) { require(sqlite3_bind_double(statement_, index, value)); }
    void bind_integer(int index, std::int64_t value) { require(sqlite3_bind_int64(statement_, index, value)); }
    [[nodiscard]] int step() { return sqlite3_step(statement_); }
    [[nodiscard]] bool is_null(int column) const noexcept { return sqlite3_column_type(statement_, column) == SQLITE_NULL; }
    [[nodiscard]] std::string text(int column) const {
        const auto* value = sqlite3_column_text(statement_, column);
        if (value == nullptr) throw SqliteError{SQLITE_MISMATCH, "Unexpected NULL in prepared-job record"};
        return {reinterpret_cast<const char*>(value), static_cast<std::size_t>(sqlite3_column_bytes(statement_, column))};
    }
    [[nodiscard]] std::int64_t integer(int column) const noexcept { return sqlite3_column_int64(statement_, column); }
private:
    void require(int result) const {
        if (result != SQLITE_OK) throw SqliteError{result, std::string{"Unable to bind prepared-job value: "} + sqlite3_errmsg(database_)};
    }
    sqlite3* database_;
    sqlite3_stmt* statement_{nullptr};
};

void require_done(sqlite3* database, Statement& statement, std::string_view operation) {
    const int result = statement.step();
    if (result != SQLITE_DONE) throw SqliteError{result, std::string{operation} + ": " + sqlite3_errmsg(database)};
}

void validate(const domain::Job& job, const application::PreparedJobExecution& execution) {
    if (job.status() != domain::JobStatus::queued || job.revision() <= 0 ||
        job.attempt_number() != 1 || execution.attempt_number != 1 ||
        execution.job_id != job.id() || execution.launch_revision != job.revision() + 1 ||
        !job.pipeline_id().has_value() || !job.pipeline_version().has_value() ||
        execution.pipeline_id != *job.pipeline_id() || execution.pipeline_version != *job.pipeline_version() ||
        execution.execution_plan_path.empty() || execution.prepared_at_utc.empty()) {
        throw std::invalid_argument("Prepared-job execution does not match the queued Job");
    }
}

void validate_retry(
    const domain::Job& job,
    const std::int64_t expected_revision,
    const application::PreparedJobExecution& execution
) {
    if (expected_revision < 0 || job.status() != domain::JobStatus::queued ||
        job.revision() != expected_revision + 1 || job.attempt_number() <= 1 ||
        execution.attempt_number != job.attempt_number() || execution.job_id != job.id() ||
        job.revision() == std::numeric_limits<std::int64_t>::max() ||
        execution.launch_revision != job.revision() + 1 ||
        !job.pipeline_id().has_value() || !job.pipeline_version().has_value() ||
        execution.pipeline_id != *job.pipeline_id() ||
        execution.pipeline_version != *job.pipeline_version() ||
        execution.execution_plan_path.empty() || execution.prepared_at_utc.empty()) {
        throw std::invalid_argument("Retry execution does not match the queued Job");
    }
}

}  // namespace

SqlitePreparedJobStore::SqlitePreparedJobStore(SqliteConnection& connection) noexcept
    : connection_{connection} {}

bool SqlitePreparedJobStore::add_prepared_job(
    const domain::Job& job,
    const application::PreparedJobExecution& execution
) {
    validate(job, execution);
    sqlite3* database = connection_.native_handle();
    Transaction transaction{connection_};

    constexpr const char* insert_job = R"sql(
        INSERT INTO jobs(
            id, analysis_id, pipeline_id, pipeline_version, status, priority, progress,
            active_step_id, created_at_utc, updated_at_utc, started_at_utc,
            finished_at_utc, revision, attempt_number
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(id) DO NOTHING;
    )sql";
    Statement job_statement{database, insert_job};
    job_statement.bind_text(1, job.id());
    job_statement.bind_optional_text(2, job.analysis_id());
    job_statement.bind_optional_text(3, job.pipeline_id());
    job_statement.bind_optional_text(4, job.pipeline_version());
    job_statement.bind_text(5, domain::to_string(job.status()));
    job_statement.bind_text(6, domain::to_string(job.priority()));
    job_statement.bind_double(7, job.progress());
    job_statement.bind_optional_text(8, job.active_step_id());
    job_statement.bind_text(9, job.created_at_utc());
    job_statement.bind_text(10, job.updated_at_utc());
    job_statement.bind_optional_text(11, job.started_at_utc());
    job_statement.bind_optional_text(12, job.finished_at_utc());
    job_statement.bind_integer(13, job.revision());
    job_statement.bind_integer(14, job.attempt_number());
    require_done(database, job_statement, "Unable to insert prepared job");
    if (sqlite3_changes(database) != 1) {
        return false;
    }

    constexpr const char* insert_execution = R"sql(
        INSERT INTO job_execution_plans(
            job_id, launch_revision, pipeline_id, pipeline_version,
            execution_plan_path, prepared_at_utc
        ) VALUES (?, ?, ?, ?, ?, ?);
    )sql";
    Statement execution_statement{database, insert_execution};
    execution_statement.bind_text(1, execution.job_id);
    execution_statement.bind_integer(2, execution.launch_revision);
    execution_statement.bind_text(3, execution.pipeline_id);
    execution_statement.bind_text(4, execution.pipeline_version);
    execution_statement.bind_text(5, execution.execution_plan_path);
    execution_statement.bind_text(6, execution.prepared_at_utc);
    require_done(database, execution_statement, "Unable to insert prepared execution plan");

    transaction.commit();
    return true;
}

bool SqlitePreparedJobStore::retry_prepared_job(
    const domain::Job& job,
    const std::int64_t expected_revision,
    const application::PreparedJobExecution& execution
) {
    validate_retry(job, expected_revision, execution);
    sqlite3* database = connection_.native_handle();
    Transaction transaction{connection_};

    constexpr const char* update_job = R"sql(
        UPDATE jobs SET
            status = ?, progress = ?, active_step_id = ?, updated_at_utc = ?,
            started_at_utc = ?, finished_at_utc = ?, revision = ?, attempt_number = ?,
            failure_kind = NULL, failure_message = NULL, failure_exit_code = NULL,
            failure_worker_timestamp_utc = NULL, failure_recorded_at_utc = NULL
        WHERE id = ? AND revision = ? AND status = 'interrupted';
    )sql";
    Statement job_statement{database, update_job};
    job_statement.bind_text(1, domain::to_string(job.status()));
    job_statement.bind_double(2, job.progress());
    job_statement.bind_optional_text(3, job.active_step_id());
    job_statement.bind_text(4, job.updated_at_utc());
    job_statement.bind_optional_text(5, job.started_at_utc());
    job_statement.bind_optional_text(6, job.finished_at_utc());
    job_statement.bind_integer(7, job.revision());
    job_statement.bind_integer(8, job.attempt_number());
    job_statement.bind_text(9, job.id());
    job_statement.bind_integer(10, expected_revision);
    require_done(database, job_statement, "Unable to persist retried job");
    if (sqlite3_changes(database) != 1) {
        return false;
    }

    constexpr const char* update_execution = R"sql(
        UPDATE job_execution_plans SET launch_revision = ? WHERE job_id = ?;
    )sql";
    Statement execution_statement{database, update_execution};
    execution_statement.bind_integer(1, execution.launch_revision);
    execution_statement.bind_text(2, execution.job_id);
    require_done(database, execution_statement, "Unable to advance retry launch revision");
    if (sqlite3_changes(database) != 1) {
        throw SqliteError{SQLITE_CONSTRAINT, "Prepared execution association is missing during retry"};
    }

    transaction.commit();
    return true;
}

std::optional<application::PreparedJobExecution> SqlitePreparedJobStore::find_execution(
    const std::string_view job_id
) {
    constexpr const char* sql = R"sql(
        SELECT p.job_id, j.attempt_number, p.launch_revision, p.pipeline_id, p.pipeline_version,
               p.execution_plan_path, p.prepared_at_utc
        FROM job_execution_plans AS p
        JOIN jobs AS j ON j.id = p.job_id
        WHERE p.job_id = ?;
    )sql";
    Statement statement{connection_.native_handle(), sql};
    statement.bind_text(1, job_id);
    const int result = statement.step();
    if (result == SQLITE_DONE) return std::nullopt;
    if (result != SQLITE_ROW) {
        throw SqliteError{result, std::string{"Unable to find prepared execution: "} + sqlite3_errmsg(connection_.native_handle())};
    }
    return application::PreparedJobExecution{
        .job_id = statement.text(0),
        .attempt_number = statement.integer(1),
        .launch_revision = statement.integer(2),
        .pipeline_id = statement.text(3),
        .pipeline_version = statement.text(4),
        .execution_plan_path = statement.text(5),
        .prepared_at_utc = statement.text(6),
    };
}

}  // namespace biocore::infrastructure::sqlite
