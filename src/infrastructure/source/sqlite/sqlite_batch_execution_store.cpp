#include "biocore/infrastructure/sqlite/sqlite_batch_execution_store.hpp"

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
    void commit() {
        connection_.execute("COMMIT;");
        committed_ = true;
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
private:
    SqliteConnection& connection_;
    bool committed_{false};
};

class Statement final {
public:
    Statement(sqlite3* database, const char* sql, std::string_view operation)
        : database_{database}, operation_{operation} {
        require(sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr));
    }
    ~Statement() { sqlite3_finalize(statement_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind_text(const int index, const std::string_view value) {
        require(sqlite3_bind_text64(
            statement_, index, value.data(),
            static_cast<sqlite3_uint64>(value.size()),
            SQLITE_TRANSIENT, SQLITE_UTF8
        ));
    }
    void bind_optional_text(const int index, const std::optional<std::string>& value) {
        if (value.has_value()) bind_text(index, *value);
        else require(sqlite3_bind_null(statement_, index));
    }
    void bind_integer(const int index, const std::int64_t value) {
        require(sqlite3_bind_int64(statement_, index, value));
    }
    void bind_double(const int index, const double value) {
        require(sqlite3_bind_double(statement_, index, value));
    }
    [[nodiscard]] int step() { return sqlite3_step(statement_); }
    [[nodiscard]] bool is_null(const int column) const noexcept {
        return sqlite3_column_type(statement_, column) == SQLITE_NULL;
    }
    [[nodiscard]] std::string text(const int column) const {
        const auto* value = sqlite3_column_text(statement_, column);
        if (value == nullptr) {
            throw SqliteError{SQLITE_MISMATCH, std::string{operation_} + ": unexpected NULL"};
        }
        return {
            reinterpret_cast<const char*>(value),
            static_cast<std::size_t>(sqlite3_column_bytes(statement_, column))
        };
    }
    [[nodiscard]] std::int64_t integer(const int column) const noexcept {
        return sqlite3_column_int64(statement_, column);
    }

private:
    void require(const int result) const {
        if (result != SQLITE_OK) {
            throw SqliteError{
                result,
                std::string{operation_} + ": " + sqlite3_errmsg(database_)
            };
        }
    }
    sqlite3* database_;
    std::string operation_;
    sqlite3_stmt* statement_{nullptr};
};

void require_done(sqlite3* database, Statement& statement, std::string_view operation) {
    const int result = statement.step();
    if (result != SQLITE_DONE) {
        throw SqliteError{
            result, std::string{operation} + ": " + sqlite3_errmsg(database)
        };
    }
}

void validate_submission(
    const application::BatchExecutionRecord& execution,
    const std::span<const application::BatchPreparedJob> jobs
) {
    if (execution.plan_id.empty() || execution.plan_id.size() > 128U ||
        execution.plan_id.find('\0') != std::string::npos ||
        execution.maximum_concurrent_jobs == 0U ||
        execution.maximum_concurrent_jobs > 64U ||
        execution.cancellation_requested ||
        execution.submitted_at_utc.empty() ||
        execution.updated_at_utc != execution.submitted_at_utc ||
        execution.jobs.empty() ||
        execution.jobs.size() != jobs.size()) {
        throw std::invalid_argument{"Batch execution submission record is invalid"};
    }

    for (std::size_t index = 0U; index < jobs.size(); ++index) {
        const auto& item = jobs[index];
        const auto& link = execution.jobs[index];
        if (link != item.link ||
            link.ordinal != index ||
            link.sample_id.empty() ||
            link.sample_id.size() > 128U ||
            link.job_id != item.job.id() ||
            item.job.status() != domain::JobStatus::queued ||
            item.job.revision() != 1 ||
            item.job.attempt_number() != 1 ||
            item.job.analysis_id() != std::optional<std::string>{execution.plan_id} ||
            !item.job.pipeline_id().has_value() ||
            !item.job.pipeline_version().has_value() ||
            item.execution.job_id != item.job.id() ||
            item.execution.attempt_number != 1 ||
            item.execution.launch_revision != 2 ||
            item.execution.pipeline_id != *item.job.pipeline_id() ||
            item.execution.pipeline_version != *item.job.pipeline_version() ||
            item.execution.execution_plan_path.empty() ||
            item.execution.prepared_at_utc.empty()) {
            throw std::invalid_argument{"Prepared batch job does not match submission ledger"};
        }
    }
}

}  // namespace

SqliteBatchExecutionStore::SqliteBatchExecutionStore(
    SqliteConnection& connection
) noexcept
    : connection_{connection} {}

application::AddBatchExecutionResult SqliteBatchExecutionStore::add(
    const application::BatchExecutionRecord& execution,
    const std::span<const application::BatchPreparedJob> jobs
) {
    validate_submission(execution, jobs);
    sqlite3* const database = connection_.native_handle();
    Transaction transaction{connection_};

    Statement parent{
        database,
        "INSERT INTO batch_executions("
        "plan_id,maximum_concurrent_jobs,cancellation_requested,"
        "submitted_at_utc,updated_at_utc,sealed"
        ") VALUES(?,?,0,?,?,0) ON CONFLICT(plan_id) DO NOTHING;",
        "Unable to insert batch execution"
    };
    parent.bind_text(1, execution.plan_id);
    parent.bind_integer(
        2, static_cast<std::int64_t>(execution.maximum_concurrent_jobs)
    );
    parent.bind_text(3, execution.submitted_at_utc);
    parent.bind_text(4, execution.updated_at_utc);
    require_done(database, parent, "Unable to insert batch execution");
    if (sqlite3_changes(database) != 1) {
        return application::AddBatchExecutionResult::plan_already_submitted;
    }

    constexpr const char* insert_job = R"sql(
        INSERT INTO jobs(
            id, analysis_id, pipeline_id, pipeline_version, status, priority, progress,
            active_step_id, created_at_utc, updated_at_utc, started_at_utc,
            finished_at_utc, revision, attempt_number
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(id) DO NOTHING;
    )sql";

    constexpr const char* insert_execution = R"sql(
        INSERT INTO job_execution_plans(
            job_id, launch_revision, pipeline_id, pipeline_version,
            execution_plan_path, prepared_at_utc
        ) VALUES (?, ?, ?, ?, ?, ?);
    )sql";

    constexpr const char* insert_link = R"sql(
        INSERT INTO batch_execution_jobs(plan_id,sample_id,ordinal,job_id)
        VALUES(?,?,?,?);
    )sql";

    for (const auto& item : jobs) {
        Statement job_statement{
            database, insert_job, "Unable to insert batch prepared job"
        };
        job_statement.bind_text(1, item.job.id());
        job_statement.bind_optional_text(2, item.job.analysis_id());
        job_statement.bind_optional_text(3, item.job.pipeline_id());
        job_statement.bind_optional_text(4, item.job.pipeline_version());
        job_statement.bind_text(5, domain::to_string(item.job.status()));
        job_statement.bind_text(6, domain::to_string(item.job.priority()));
        job_statement.bind_double(7, item.job.progress());
        job_statement.bind_optional_text(8, item.job.active_step_id());
        job_statement.bind_text(9, item.job.created_at_utc());
        job_statement.bind_text(10, item.job.updated_at_utc());
        job_statement.bind_optional_text(11, item.job.started_at_utc());
        job_statement.bind_optional_text(12, item.job.finished_at_utc());
        job_statement.bind_integer(13, item.job.revision());
        job_statement.bind_integer(14, item.job.attempt_number());
        require_done(database, job_statement, "Unable to insert batch prepared job");
        if (sqlite3_changes(database) != 1) {
            return application::AddBatchExecutionResult::job_identifier_conflict;
        }

        Statement execution_statement{
            database, insert_execution, "Unable to insert batch execution plan"
        };
        execution_statement.bind_text(1, item.execution.job_id);
        execution_statement.bind_integer(2, item.execution.launch_revision);
        execution_statement.bind_text(3, item.execution.pipeline_id);
        execution_statement.bind_text(4, item.execution.pipeline_version);
        execution_statement.bind_text(5, item.execution.execution_plan_path);
        execution_statement.bind_text(6, item.execution.prepared_at_utc);
        require_done(
            database, execution_statement, "Unable to insert batch execution plan"
        );

        Statement link_statement{
            database, insert_link, "Unable to insert batch execution sample link"
        };
        link_statement.bind_text(1, execution.plan_id);
        link_statement.bind_text(2, item.link.sample_id);
        link_statement.bind_integer(
            3, static_cast<std::int64_t>(item.link.ordinal)
        );
        link_statement.bind_text(4, item.link.job_id);
        require_done(
            database, link_statement, "Unable to insert batch execution sample link"
        );
    }

    Statement seal{
        database,
        "UPDATE batch_executions SET sealed=1 WHERE plan_id=? AND sealed=0;",
        "Unable to seal batch execution"
    };
    seal.bind_text(1, execution.plan_id);
    require_done(database, seal, "Unable to seal batch execution");
    if (sqlite3_changes(database) != 1) {
        throw SqliteError{SQLITE_CONSTRAINT, "Batch execution could not be sealed"};
    }

    transaction.commit();
    return application::AddBatchExecutionResult::created;
}

std::optional<application::BatchExecutionRecord> SqliteBatchExecutionStore::find(
    const std::string_view plan_id
) {
    sqlite3* const database = connection_.native_handle();
    Statement parent{
        database,
        "SELECT maximum_concurrent_jobs,cancellation_requested,"
        "submitted_at_utc,updated_at_utc,sealed "
        "FROM batch_executions WHERE plan_id=?;",
        "Unable to find batch execution"
    };
    parent.bind_text(1, plan_id);
    const int result = parent.step();
    if (result == SQLITE_DONE) return std::nullopt;
    if (result != SQLITE_ROW) {
        throw SqliteError{
            result, std::string{"Unable to find batch execution: "} +
                sqlite3_errmsg(database)
        };
    }
    if (parent.integer(4) != 1 ||
        parent.integer(0) <= 0 ||
        parent.integer(0) > 64 ||
        (parent.integer(1) != 0 && parent.integer(1) != 1)) {
        throw SqliteError{SQLITE_CORRUPT, "Batch execution ledger is invalid"};
    }

    application::BatchExecutionRecord execution{
        .plan_id = std::string{plan_id},
        .maximum_concurrent_jobs =
            static_cast<std::size_t>(parent.integer(0)),
        .cancellation_requested = parent.integer(1) == 1,
        .submitted_at_utc = parent.text(2),
        .updated_at_utc = parent.text(3),
        .jobs = {},
    };

    Statement jobs{
        database,
        "SELECT sample_id,ordinal,job_id FROM batch_execution_jobs "
        "WHERE plan_id=? ORDER BY ordinal;",
        "Unable to list batch execution jobs"
    };
    jobs.bind_text(1, plan_id);
    for (;;) {
        const int step = jobs.step();
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW) {
            throw SqliteError{
                step, std::string{"Unable to list batch execution jobs: "} +
                    sqlite3_errmsg(database)
            };
        }
        const std::int64_t ordinal = jobs.integer(1);
        if (ordinal < 0 ||
            static_cast<std::uint64_t>(ordinal) >
                std::numeric_limits<std::size_t>::max()) {
            throw SqliteError{SQLITE_CORRUPT, "Batch execution ordinal is invalid"};
        }
        execution.jobs.push_back(application::BatchExecutionJobLink{
            .sample_id = jobs.text(0),
            .ordinal = static_cast<std::size_t>(ordinal),
            .job_id = jobs.text(2),
        });
    }
    if (execution.jobs.empty()) {
        throw SqliteError{SQLITE_CORRUPT, "Sealed batch execution has no jobs"};
    }
    return execution;
}

bool SqliteBatchExecutionStore::request_cancellation(
    const std::string_view plan_id,
    const std::string_view updated_at_utc
) {
    sqlite3* const database = connection_.native_handle();
    Transaction transaction{connection_};

    Statement update{
        database,
        "UPDATE batch_executions SET cancellation_requested=1,updated_at_utc=? "
        "WHERE plan_id=? AND sealed=1 AND cancellation_requested=0;",
        "Unable to request batch cancellation"
    };
    update.bind_text(1, updated_at_utc);
    update.bind_text(2, plan_id);
    require_done(database, update, "Unable to request batch cancellation");
    if (sqlite3_changes(database) == 1) {
        transaction.commit();
        return true;
    }

    Statement exists{
        database,
        "SELECT cancellation_requested FROM batch_executions "
        "WHERE plan_id=? AND sealed=1;",
        "Unable to inspect batch cancellation"
    };
    exists.bind_text(1, plan_id);
    const int result = exists.step();
    if (result == SQLITE_DONE) return false;
    if (result != SQLITE_ROW) {
        throw SqliteError{
            result, std::string{"Unable to inspect batch cancellation: "} +
                sqlite3_errmsg(database)
        };
    }
    const bool already_requested = exists.integer(0) == 1;
    transaction.commit();
    return already_requested;
}

std::optional<application::BatchSchedulingQuota>
SqliteBatchExecutionStore::quota_for_job(const std::string_view job_id) {
    Statement query{
        connection_.native_handle(),
        "SELECT e.plan_id,e.maximum_concurrent_jobs "
        "FROM batch_execution_jobs AS j "
        "JOIN batch_executions AS e ON e.plan_id=j.plan_id "
        "WHERE j.job_id=? AND e.sealed=1;",
        "Unable to read batch scheduling quota"
    };
    query.bind_text(1, job_id);
    const int result = query.step();
    if (result == SQLITE_DONE) return std::nullopt;
    if (result != SQLITE_ROW) {
        throw SqliteError{
            result, std::string{"Unable to read batch scheduling quota: "} +
                sqlite3_errmsg(connection_.native_handle())
        };
    }
    const std::int64_t limit = query.integer(1);
    if (limit <= 0 || limit > 64) {
        throw SqliteError{SQLITE_CORRUPT, "Batch scheduling quota is invalid"};
    }
    return application::BatchSchedulingQuota{
        .group_id = query.text(0),
        .maximum_concurrent_jobs = static_cast<std::size_t>(limit),
    };
}

}  // namespace biocore::infrastructure::sqlite
