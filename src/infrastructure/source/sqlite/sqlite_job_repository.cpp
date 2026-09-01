#include "biocore/infrastructure/sqlite/sqlite_job_repository.hpp"

#include <sqlite3.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/domain/job.hpp"
#include "biocore/domain/job_failure.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/domain/job_status.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"

namespace biocore::infrastructure::sqlite {
namespace {

class Statement final {
public:
    Statement(sqlite3* database, const char* sql) : database_{database} {
        const int result = sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr);
        if (result != SQLITE_OK) {
            throw SqliteError{result, std::string{"Unable to prepare job statement: "} + sqlite3_errmsg(database_)};
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
        const int result = sqlite3_bind_text64(
            statement_, index, value.data(), static_cast<sqlite3_uint64>(value.size()), SQLITE_TRANSIENT, SQLITE_UTF8
        );
        require_bind(result);
    }

    void bind_optional_text(const int index, const std::optional<std::string>& value) {
        if (value.has_value()) {
            bind_text(index, *value);
            return;
        }
        require_bind(sqlite3_bind_null(statement_, index));
    }

    void bind_null(const int index) {
        require_bind(sqlite3_bind_null(statement_, index));
    }

    void bind_double(const int index, const double value) {
        require_bind(sqlite3_bind_double(statement_, index, value));
    }

    void bind_integer(const int index, const std::int64_t value) {
        require_bind(sqlite3_bind_int64(statement_, index, value));
    }

    [[nodiscard]] int step() {
        return sqlite3_step(statement_);
    }

    [[nodiscard]] bool is_null(const int column) const noexcept {
        return sqlite3_column_type(statement_, column) == SQLITE_NULL;
    }

    [[nodiscard]] std::string text(const int column) const {
        const auto* value = sqlite3_column_text(statement_, column);
        if (value == nullptr) {
            throw SqliteError{SQLITE_MISMATCH, "Unexpected NULL in job record"};
        }
        return {
            reinterpret_cast<const char*>(value),
            static_cast<std::size_t>(sqlite3_column_bytes(statement_, column)),
        };
    }

    [[nodiscard]] std::optional<std::string> optional_text(const int column) const {
        if (is_null(column)) {
            return std::nullopt;
        }
        return text(column);
    }

    [[nodiscard]] double real(const int column) const noexcept {
        return sqlite3_column_double(statement_, column);
    }

    [[nodiscard]] std::int64_t integer(const int column) const noexcept {
        return sqlite3_column_int64(statement_, column);
    }

private:
    void require_bind(const int result) const {
        if (result != SQLITE_OK) {
            throw SqliteError{result, std::string{"Unable to bind job value: "} + sqlite3_errmsg(database_)};
        }
    }

    sqlite3* database_;
    sqlite3_stmt* statement_{nullptr};
};

[[nodiscard]] std::optional<domain::JobFailure> read_failure(const Statement& statement) {
    if (statement.is_null(13)) {
        if (!statement.is_null(14) || !statement.is_null(15) || !statement.is_null(16) ||
            !statement.is_null(17)) {
            throw SqliteError{SQLITE_MISMATCH, "Job failure record is only partially NULL"};
        }
        return std::nullopt;
    }

    if (statement.is_null(14) || statement.is_null(17)) {
        throw SqliteError{SQLITE_MISMATCH, "Job failure record is incomplete"};
    }
    const auto kind = domain::job_failure_kind_from_string(statement.text(13));
    if (!kind.has_value()) {
        throw SqliteError{SQLITE_MISMATCH, "Job failure record contains an unsupported kind"};
    }
    try {
        return domain::JobFailure{
            *kind,
            statement.text(14),
            statement.is_null(15) ? std::nullopt
                                  : std::optional<std::int64_t>{statement.integer(15)},
            statement.optional_text(16),
            statement.text(17),
        };
    } catch (const std::invalid_argument& error) {
        throw SqliteError{
            SQLITE_MISMATCH,
            std::string{"Job failure record violates domain invariants: "} + error.what(),
        };
    }
}

[[nodiscard]] domain::Job read_job(const Statement& statement) {
    const auto status = domain::job_status_from_string(statement.text(4));
    const auto priority = domain::job_priority_from_string(statement.text(5));
    if (!status.has_value() || !priority.has_value()) {
        throw SqliteError{SQLITE_MISMATCH, "Job record contains an unsupported enum value"};
    }

    try {
        return domain::Job{
            statement.text(0),
            statement.optional_text(1),
            statement.optional_text(2),
            statement.optional_text(3),
            *status,
            *priority,
            statement.real(6),
            statement.optional_text(7),
            statement.text(8),
            statement.text(9),
            statement.optional_text(10),
            statement.optional_text(11),
            statement.integer(12),
            read_failure(statement),
        };
    } catch (const std::invalid_argument& error) {
        throw SqliteError{
            SQLITE_MISMATCH,
            std::string{"Job record violates domain invariants: "} + error.what(),
        };
    }
}

constexpr const char* select_columns = R"sql(
    id,
    analysis_id,
    pipeline_id,
    pipeline_version,
    status,
    priority,
    progress,
    active_step_id,
    created_at_utc,
    updated_at_utc,
    started_at_utc,
    finished_at_utc,
    revision,
    failure_kind,
    failure_message,
    failure_exit_code,
    failure_worker_timestamp_utc,
    failure_recorded_at_utc
)sql";

void require_done(sqlite3* database, Statement& statement, const std::string_view operation) {
    const int result = statement.step();
    if (result != SQLITE_DONE) {
        throw SqliteError{result, std::string{operation} + ": " + sqlite3_errmsg(database)};
    }
}

}  // namespace

SqliteJobRepository::SqliteJobRepository(SqliteConnection& connection) noexcept : connection_{connection} {}

bool SqliteJobRepository::add(const domain::Job& job) {
    constexpr const char* sql = R"sql(
        INSERT INTO jobs(
            id, analysis_id, pipeline_id, pipeline_version, status, priority, progress,
            active_step_id, created_at_utc, updated_at_utc, started_at_utc,
            finished_at_utc, revision, failure_kind, failure_message, failure_exit_code,
            failure_worker_timestamp_utc, failure_recorded_at_utc
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT(id) DO NOTHING;
    )sql";

    sqlite3* database = connection_.native_handle();
    Statement statement{database, sql};
    statement.bind_text(1, job.id());
    statement.bind_optional_text(2, job.analysis_id());
    statement.bind_optional_text(3, job.pipeline_id());
    statement.bind_optional_text(4, job.pipeline_version());
    statement.bind_text(5, domain::to_string(job.status()));
    statement.bind_text(6, domain::to_string(job.priority()));
    statement.bind_double(7, job.progress());
    statement.bind_optional_text(8, job.active_step_id());
    statement.bind_text(9, job.created_at_utc());
    statement.bind_text(10, job.updated_at_utc());
    statement.bind_optional_text(11, job.started_at_utc());
    statement.bind_optional_text(12, job.finished_at_utc());
    statement.bind_integer(13, job.revision());
    if (job.failure().has_value()) {
        statement.bind_text(14, domain::to_string(job.failure()->kind()));
        statement.bind_text(15, job.failure()->message());
        if (job.failure()->exit_code().has_value()) {
            statement.bind_integer(16, *job.failure()->exit_code());
        } else {
            statement.bind_null(16);
        }
        statement.bind_optional_text(17, job.failure()->worker_timestamp_utc());
        statement.bind_text(18, job.failure()->recorded_at_utc());
    } else {
        statement.bind_null(14);
        statement.bind_null(15);
        statement.bind_null(16);
        statement.bind_null(17);
        statement.bind_null(18);
    }
    require_done(database, statement, "Unable to insert job");
    return sqlite3_changes(database) == 1;
}

std::optional<domain::Job> SqliteJobRepository::find_by_id(const std::string_view job_id) {
    const std::string sql = std::string{"SELECT "} + select_columns + " FROM jobs WHERE id = ?;";
    Statement statement{connection_.native_handle(), sql.c_str()};
    statement.bind_text(1, job_id);
    const int result = statement.step();
    if (result == SQLITE_DONE) {
        return std::nullopt;
    }
    if (result != SQLITE_ROW) {
        throw SqliteError{result, std::string{"Unable to find job: "} + sqlite3_errmsg(connection_.native_handle())};
    }
    return read_job(statement);
}

std::vector<domain::Job> SqliteJobRepository::list() {
    const std::string sql = std::string{"SELECT "} + select_columns + " FROM jobs ORDER BY created_at_utc, id;";
    Statement statement{connection_.native_handle(), sql.c_str()};
    std::vector<domain::Job> jobs;
    for (;;) {
        const int result = statement.step();
        if (result == SQLITE_DONE) {
            break;
        }
        if (result != SQLITE_ROW) {
            throw SqliteError{result, std::string{"Unable to list jobs: "} + sqlite3_errmsg(connection_.native_handle())};
        }
        jobs.push_back(read_job(statement));
    }
    return jobs;
}

bool SqliteJobRepository::update_runtime_state(
    const domain::Job& job,
    const std::int64_t expected_revision
) {
    if (expected_revision < 0 || job.revision() <= expected_revision ||
        job.revision() - expected_revision != 1) {
        throw std::invalid_argument("Job runtime updates must advance the revision by exactly one");
    }

    constexpr const char* sql = R"sql(
        UPDATE jobs SET
            status = ?,
            progress = ?,
            active_step_id = ?,
            updated_at_utc = ?,
            started_at_utc = ?,
            finished_at_utc = ?,
            revision = ?,
            failure_kind = ?,
            failure_message = ?,
            failure_exit_code = ?,
            failure_worker_timestamp_utc = ?,
            failure_recorded_at_utc = ?
        WHERE id = ? AND revision = ?;
    )sql";

    sqlite3* database = connection_.native_handle();
    Statement statement{database, sql};
    statement.bind_text(1, domain::to_string(job.status()));
    statement.bind_double(2, job.progress());
    statement.bind_optional_text(3, job.active_step_id());
    statement.bind_text(4, job.updated_at_utc());
    statement.bind_optional_text(5, job.started_at_utc());
    statement.bind_optional_text(6, job.finished_at_utc());
    statement.bind_integer(7, job.revision());
    if (job.failure().has_value()) {
        statement.bind_text(8, domain::to_string(job.failure()->kind()));
        statement.bind_text(9, job.failure()->message());
        if (job.failure()->exit_code().has_value()) {
            statement.bind_integer(10, *job.failure()->exit_code());
        } else {
            statement.bind_null(10);
        }
        statement.bind_optional_text(11, job.failure()->worker_timestamp_utc());
        statement.bind_text(12, job.failure()->recorded_at_utc());
    } else {
        statement.bind_null(8);
        statement.bind_null(9);
        statement.bind_null(10);
        statement.bind_null(11);
        statement.bind_null(12);
    }
    statement.bind_text(13, job.id());
    statement.bind_integer(14, expected_revision);
    require_done(database, statement, "Unable to update job runtime state");
    return sqlite3_changes(database) == 1;
}

}  // namespace biocore::infrastructure::sqlite
