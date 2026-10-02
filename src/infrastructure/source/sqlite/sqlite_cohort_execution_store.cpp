#include "biocore/infrastructure/sqlite/sqlite_cohort_execution_store.hpp"

#include <sqlite3.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/cohort_execution.hpp"
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

class Statement final {
public:
    Statement(sqlite3* database, const char* sql, std::string description)
        : database_{database}, description_{std::move(description)} {
        const int result = sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr);
        if (result != SQLITE_OK) {
            throw SqliteError{result, description_ + ": " + sqlite3_errmsg(database_)};
        }
    }
    ~Statement() { if (statement_ != nullptr) sqlite3_finalize(statement_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind_text(const int index, const std::string_view value) {
        require(sqlite3_bind_text64(
            statement_, index, value.data(),
            static_cast<sqlite3_uint64>(value.size()),
            SQLITE_TRANSIENT, SQLITE_UTF8
        ));
    }
    void bind_integer(const int index, const std::int64_t value) {
        require(sqlite3_bind_int64(statement_, index, value));
    }
    void bind_null(const int index) { require(sqlite3_bind_null(statement_, index)); }
    void bind_optional_text(const int index, const std::optional<std::string>& value) {
        if (value.has_value()) bind_text(index, *value);
        else bind_null(index);
    }
    [[nodiscard]] int step() { return sqlite3_step(statement_); }
    [[nodiscard]] std::string text(const int column) const {
        const auto* raw = sqlite3_column_text(statement_, column);
        if (raw == nullptr) {
            throw SqliteError{SQLITE_MISMATCH, description_ + ": unexpected NULL text"};
        }
        return {
            reinterpret_cast<const char*>(raw),
            static_cast<std::size_t>(sqlite3_column_bytes(statement_, column))
        };
    }
    [[nodiscard]] std::optional<std::string> optional_text(const int column) const {
        if (sqlite3_column_type(statement_, column) == SQLITE_NULL) return std::nullopt;
        return text(column);
    }
    [[nodiscard]] std::int64_t integer(const int column) const noexcept {
        return sqlite3_column_int64(statement_, column);
    }
private:
    void require(const int result) const {
        if (result != SQLITE_OK) {
            throw SqliteError{result, description_ + ": " + sqlite3_errmsg(database_)};
        }
    }
    sqlite3* database_;
    std::string description_;
    sqlite3_stmt* statement_{nullptr};
};

void require_done(sqlite3* database, const int result, const std::string_view message) {
    if (result != SQLITE_DONE) {
        throw SqliteError{result, std::string{message} + ": " + sqlite3_errmsg(database)};
    }
}

[[nodiscard]] bool valid_transition(
    const application::CohortExecutionState from,
    const application::CohortExecutionState to
) noexcept {
    using application::CohortExecutionState;
    if (from == to) return true;
    if (from == CohortExecutionState::completed ||
        from == CohortExecutionState::failed ||
        from == CohortExecutionState::cancelled ||
        from == CohortExecutionState::interrupted) {
        return false;
    }
    if (to == CohortExecutionState::completed) return false;
    if (from == CohortExecutionState::queued) {
        return to == CohortExecutionState::running ||
               to == CohortExecutionState::failed ||
               to == CohortExecutionState::cancelled ||
               to == CohortExecutionState::interrupted;
    }
    return to == CohortExecutionState::failed ||
           to == CohortExecutionState::cancelled ||
           to == CohortExecutionState::interrupted;
}

[[nodiscard]] application::CohortExecutionAttempt read_attempt(Statement& row) {
    const auto state =
        application::cohort_execution_state_from_string(row.text(10));
    if (!state.has_value()) {
        throw SqliteError{SQLITE_CORRUPT, "Cohort execution state is invalid"};
    }
    return {
        .project_id = row.text(0),
        .analysis_id = row.text(1),
        .attempt_number = row.integer(2),
        .attempt_id = row.text(3),
        .parent_attempt_id = row.optional_text(4),
        .job_id = row.optional_text(5),
        .snapshot_digest = row.text(6),
        .idempotency_key = row.text(7),
        .payload_digest = row.text(8),
        .state = *state,
        .cancellation_requested = row.integer(9) == 1,
        .created_at_utc = row.text(11),
        .updated_at_utc = row.text(12),
        .failure_message = row.optional_text(13),
        .result_manifest_file_id = row.optional_text(14),
        .result_manifest_sha256 = row.optional_text(15),
    };
}

constexpr const char* select_columns =
    "project_id,analysis_id,attempt_number,attempt_id,parent_attempt_id,job_id,"
    "snapshot_digest,idempotency_key,payload_digest,cancellation_requested,state,"
    "created_at_utc,updated_at_utc,failure_message,result_manifest_file_id,"
    "result_manifest_sha256";

void validate_new_attempt(const application::CohortExecutionAttempt& attempt) {
    if (attempt.project_id.empty() || attempt.analysis_id.empty() ||
        attempt.attempt_id.empty() || attempt.idempotency_key.empty() ||
        attempt.snapshot_digest.size() != 64U ||
        attempt.payload_digest.size() != 64U ||
        attempt.attempt_number < 1 ||
        attempt.job_id.has_value() ||
        attempt.state != application::CohortExecutionState::queued ||
        attempt.cancellation_requested ||
        attempt.created_at_utc.empty() || attempt.updated_at_utc.empty() ||
        attempt.failure_message.has_value() ||
        attempt.result_manifest_file_id.has_value() ||
        attempt.result_manifest_sha256.has_value()) {
        throw std::invalid_argument("New cohort execution reservation is invalid");
    }
    if ((attempt.attempt_number == 1) != !attempt.parent_attempt_id.has_value()) {
        throw std::invalid_argument("Cohort execution parent lineage is invalid");
    }
}

[[nodiscard]] std::optional<application::CohortExecutionAttempt>
find_idempotency(
    sqlite3* database,
    const std::string_view project_id,
    const std::string_view analysis_id,
    const std::string_view key
) {
    const std::string sql =
        std::string{"SELECT "} + select_columns +
        " FROM cohort_analysis_attempts "
        "WHERE project_id=? AND analysis_id=? AND idempotency_key=?;";
    Statement query{database, sql.c_str(), "Unable to inspect cohort execution idempotency"};
    query.bind_text(1, project_id);
    query.bind_text(2, analysis_id);
    query.bind_text(3, key);
    const int result = query.step();
    if (result == SQLITE_DONE) return std::nullopt;
    if (result != SQLITE_ROW) {
        throw SqliteError{
            result,
            std::string{"Unable to inspect cohort execution idempotency: "} +
                sqlite3_errmsg(database)
        };
    }
    return read_attempt(query);
}

void insert_attempt(
    sqlite3* database,
    const application::CohortExecutionAttempt& attempt
) {
    Statement insert{
        database,
        "INSERT INTO cohort_analysis_attempts("
        "project_id,analysis_id,attempt_number,attempt_id,parent_attempt_id,job_id,"
        "snapshot_digest,idempotency_key,payload_digest,cancellation_requested,state,"
        "created_at_utc,updated_at_utc,failure_message,result_manifest_file_id,"
        "result_manifest_sha256"
        ") VALUES(?,?,?,?,?,NULL,?,?,?,0,'queued',?,?,NULL,NULL,NULL);",
        "Unable to reserve cohort execution attempt"
    };
    insert.bind_text(1, attempt.project_id);
    insert.bind_text(2, attempt.analysis_id);
    insert.bind_integer(3, attempt.attempt_number);
    insert.bind_text(4, attempt.attempt_id);
    insert.bind_optional_text(5, attempt.parent_attempt_id);
    insert.bind_text(6, attempt.snapshot_digest);
    insert.bind_text(7, attempt.idempotency_key);
    insert.bind_text(8, attempt.payload_digest);
    insert.bind_text(9, attempt.created_at_utc);
    insert.bind_text(10, attempt.updated_at_utc);
    require_done(database, insert.step(), "Unable to reserve cohort execution attempt");
}

}  // namespace

SqliteCohortExecutionStore::SqliteCohortExecutionStore(
    SqliteConnection& connection
) noexcept : connection_{connection} {}

application::CohortExecutionReserveResult
SqliteCohortExecutionStore::reserve_initial(
    const application::CohortExecutionAttempt& attempt
) {
    validate_new_attempt(attempt);
    if (attempt.attempt_number != 1 || attempt.parent_attempt_id.has_value()) {
        throw std::invalid_argument("Initial cohort execution attempt must be attempt one");
    }

    sqlite3* const database = connection_.native_handle();
    Transaction transaction{connection_};

    const auto idem = find_idempotency(
        database, attempt.project_id, attempt.analysis_id, attempt.idempotency_key
    );
    if (idem.has_value()) {
        transaction.commit();
        return idem->payload_digest == attempt.payload_digest &&
                       idem->snapshot_digest == attempt.snapshot_digest
            ? application::CohortExecutionReserveResult::replayed
            : application::CohortExecutionReserveResult::idempotency_conflict;
    }

    Statement existing{
        database,
        "SELECT 1 FROM cohort_analysis_attempts "
        "WHERE project_id=? AND analysis_id=? LIMIT 1;",
        "Unable to inspect existing cohort execution"
    };
    existing.bind_text(1, attempt.project_id);
    existing.bind_text(2, attempt.analysis_id);
    const int exists = existing.step();
    if (exists == SQLITE_ROW) {
        transaction.commit();
        return application::CohortExecutionReserveResult::analysis_already_submitted;
    }
    if (exists != SQLITE_DONE) {
        throw SqliteError{
            exists,
            std::string{"Unable to inspect existing cohort execution: "} +
                sqlite3_errmsg(database)
        };
    }

    insert_attempt(database, attempt);
    transaction.commit();
    return application::CohortExecutionReserveResult::created;
}

application::CohortExecutionReserveResult
SqliteCohortExecutionStore::reserve_retry(
    const application::CohortExecutionAttempt& attempt,
    const std::string_view expected_parent_attempt_id
) {
    validate_new_attempt(attempt);
    if (attempt.attempt_number <= 1 ||
        !attempt.parent_attempt_id.has_value() ||
        *attempt.parent_attempt_id != expected_parent_attempt_id) {
        throw std::invalid_argument("Retry cohort execution lineage is invalid");
    }

    sqlite3* const database = connection_.native_handle();
    Transaction transaction{connection_};

    const auto idem = find_idempotency(
        database, attempt.project_id, attempt.analysis_id, attempt.idempotency_key
    );
    if (idem.has_value()) {
        transaction.commit();
        return idem->payload_digest == attempt.payload_digest &&
                       idem->snapshot_digest == attempt.snapshot_digest
            ? application::CohortExecutionReserveResult::replayed
            : application::CohortExecutionReserveResult::idempotency_conflict;
    }

    const std::string sql =
        std::string{"SELECT "} + select_columns +
        " FROM cohort_analysis_attempts WHERE project_id=? AND analysis_id=? "
        "ORDER BY attempt_number DESC LIMIT 1;";
    Statement latest{database, sql.c_str(), "Unable to inspect latest cohort execution"};
    latest.bind_text(1, attempt.project_id);
    latest.bind_text(2, attempt.analysis_id);
    const int result = latest.step();
    if (result != SQLITE_ROW) {
        if (result == SQLITE_DONE) {
            transaction.commit();
            return application::CohortExecutionReserveResult::retry_conflict;
        }
        throw SqliteError{
            result,
            std::string{"Unable to inspect latest cohort execution: "} +
                sqlite3_errmsg(database)
        };
    }
    const auto parent = read_attempt(latest);
    if (parent.attempt_id != expected_parent_attempt_id ||
        !application::is_retryable(parent.state) ||
        attempt.attempt_number != parent.attempt_number + 1 ||
        attempt.snapshot_digest != parent.snapshot_digest) {
        transaction.commit();
        return application::CohortExecutionReserveResult::retry_conflict;
    }

    insert_attempt(database, attempt);
    transaction.commit();
    return application::CohortExecutionReserveResult::created;
}

bool SqliteCohortExecutionStore::attach_job(
    const std::string_view project_id,
    const std::string_view analysis_id,
    const std::string_view attempt_id,
    const std::string_view job_id,
    const std::string_view updated_at_utc
) {
    Statement update{
        connection_.native_handle(),
        "UPDATE cohort_analysis_attempts SET job_id=?,updated_at_utc=? "
        "WHERE project_id=? AND analysis_id=? AND attempt_id=? "
        "AND job_id IS NULL AND state='queued' AND cancellation_requested=0;",
        "Unable to attach cohort execution Job"
    };
    update.bind_text(1, job_id);
    update.bind_text(2, updated_at_utc);
    update.bind_text(3, project_id);
    update.bind_text(4, analysis_id);
    update.bind_text(5, attempt_id);
    require_done(
        connection_.native_handle(), update.step(),
        "Unable to attach cohort execution Job"
    );
    return sqlite3_changes(connection_.native_handle()) == 1;
}

std::optional<application::CohortExecutionAttempt>
SqliteCohortExecutionStore::find_attempt(
    const std::string_view project_id,
    const std::string_view analysis_id,
    const std::string_view attempt_id
) {
    const std::string sql =
        std::string{"SELECT "} + select_columns +
        " FROM cohort_analysis_attempts "
        "WHERE project_id=? AND analysis_id=? AND attempt_id=?;";
    Statement query{
        connection_.native_handle(), sql.c_str(),
        "Unable to find cohort execution attempt"
    };
    query.bind_text(1, project_id);
    query.bind_text(2, analysis_id);
    query.bind_text(3, attempt_id);
    const int result = query.step();
    if (result == SQLITE_DONE) return std::nullopt;
    if (result != SQLITE_ROW) {
        throw SqliteError{
            result,
            std::string{"Unable to find cohort execution attempt: "} +
                sqlite3_errmsg(connection_.native_handle())
        };
    }
    return read_attempt(query);
}

std::vector<application::CohortExecutionAttempt>
SqliteCohortExecutionStore::list_attempts(
    const std::string_view project_id,
    const std::string_view analysis_id
) {
    const std::string sql =
        std::string{"SELECT "} + select_columns +
        " FROM cohort_analysis_attempts "
        "WHERE project_id=? AND analysis_id=? ORDER BY attempt_number;";
    Statement query{
        connection_.native_handle(), sql.c_str(),
        "Unable to list cohort execution attempts"
    };
    query.bind_text(1, project_id);
    query.bind_text(2, analysis_id);

    std::vector<application::CohortExecutionAttempt> values;
    for (;;) {
        const int result = query.step();
        if (result == SQLITE_DONE) break;
        if (result != SQLITE_ROW) {
            throw SqliteError{
                result,
                std::string{"Unable to list cohort execution attempts: "} +
                    sqlite3_errmsg(connection_.native_handle())
            };
        }
        values.push_back(read_attempt(query));
    }
    return values;
}

bool SqliteCohortExecutionStore::request_cancellation(
    const std::string_view project_id,
    const std::string_view analysis_id,
    const std::string_view attempt_id,
    const std::string_view updated_at_utc
) {
    sqlite3* const database = connection_.native_handle();
    Transaction transaction{connection_};
    Statement update{
        database,
        "UPDATE cohort_analysis_attempts "
        "SET cancellation_requested=1,updated_at_utc=? "
        "WHERE project_id=? AND analysis_id=? AND attempt_id=? "
        "AND state!='completed' AND cancellation_requested=0;",
        "Unable to request cohort cancellation"
    };
    update.bind_text(1, updated_at_utc);
    update.bind_text(2, project_id);
    update.bind_text(3, analysis_id);
    update.bind_text(4, attempt_id);
    require_done(database, update.step(), "Unable to request cohort cancellation");
    if (sqlite3_changes(database) == 1) {
        transaction.commit();
        return true;
    }

    Statement inspect{
        database,
        "SELECT cancellation_requested,state FROM cohort_analysis_attempts "
        "WHERE project_id=? AND analysis_id=? AND attempt_id=?;",
        "Unable to inspect cohort cancellation"
    };
    inspect.bind_text(1, project_id);
    inspect.bind_text(2, analysis_id);
    inspect.bind_text(3, attempt_id);
    const int result = inspect.step();
    if (result != SQLITE_ROW) {
        if (result == SQLITE_DONE) return false;
        throw SqliteError{
            result,
            std::string{"Unable to inspect cohort cancellation: "} +
                sqlite3_errmsg(database)
        };
    }
    const bool already = inspect.integer(0) == 1 && inspect.text(1) != "completed";
    transaction.commit();
    return already;
}

bool SqliteCohortExecutionStore::update_runtime_state(
    const std::string_view project_id,
    const std::string_view analysis_id,
    const std::string_view attempt_id,
    const application::CohortExecutionState state,
    const std::optional<std::string_view> failure_message,
    const std::string_view updated_at_utc
) {
    auto current = find_attempt(project_id, analysis_id, attempt_id);
    if (!current.has_value() || !valid_transition(current->state, state)) return false;
    const bool failure_state =
        state == application::CohortExecutionState::failed ||
        state == application::CohortExecutionState::interrupted;
    if (failure_state != failure_message.has_value()) {
        throw std::invalid_argument(
            "Cohort failed/interrupted state requires failure evidence and other states forbid it"
        );
    }
    if (current->state == state &&
        current->failure_message ==
            (failure_message.has_value()
                ? std::optional<std::string>{*failure_message}
                : std::nullopt)) {
        return true;
    }

    Statement update{
        connection_.native_handle(),
        "UPDATE cohort_analysis_attempts SET state=?,failure_message=?,updated_at_utc=? "
        "WHERE project_id=? AND analysis_id=? AND attempt_id=? "
        "AND state=? AND result_manifest_file_id IS NULL;",
        "Unable to update cohort execution state"
    };
    update.bind_text(1, application::to_string(state));
    if (failure_message.has_value()) update.bind_text(2, *failure_message);
    else update.bind_null(2);
    update.bind_text(3, updated_at_utc);
    update.bind_text(4, project_id);
    update.bind_text(5, analysis_id);
    update.bind_text(6, attempt_id);
    update.bind_text(7, application::to_string(current->state));
    require_done(
        connection_.native_handle(), update.step(),
        "Unable to update cohort execution state"
    );
    return sqlite3_changes(connection_.native_handle()) == 1;
}

bool SqliteCohortExecutionStore::record_completion(
    const std::string_view project_id,
    const std::string_view analysis_id,
    const std::string_view attempt_id,
    const std::string_view result_manifest_file_id,
    const std::string_view result_manifest_sha256,
    const std::string_view updated_at_utc
) {
    Statement update{
        connection_.native_handle(),
        "UPDATE cohort_analysis_attempts "
        "SET state='completed',result_manifest_file_id=?,result_manifest_sha256=?,"
        "failure_message=NULL,updated_at_utc=? "
        "WHERE project_id=? AND analysis_id=? AND attempt_id=? "
        "AND job_id IS NOT NULL AND state IN ('queued','running') "
        "AND cancellation_requested=0 AND result_manifest_file_id IS NULL;",
        "Unable to complete cohort execution"
    };
    update.bind_text(1, result_manifest_file_id);
    update.bind_text(2, result_manifest_sha256);
    update.bind_text(3, updated_at_utc);
    update.bind_text(4, project_id);
    update.bind_text(5, analysis_id);
    update.bind_text(6, attempt_id);
    require_done(
        connection_.native_handle(), update.step(),
        "Unable to complete cohort execution"
    );
    return sqlite3_changes(connection_.native_handle()) == 1;
}

}  // namespace biocore::infrastructure::sqlite
