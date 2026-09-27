#include "biocore/infrastructure/sqlite/sqlite_project_sample_store.hpp"

#include <sqlite3.h>

#include <stdexcept>
#include <string>
#include <unordered_set>

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
    Statement(sqlite3* database, const char* sql) : database_{database} {
        require(sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr));
    }
    ~Statement() { sqlite3_finalize(statement_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind(const int index, const std::string_view value) {
        require(sqlite3_bind_text64(statement_, index, value.data(),
            static_cast<sqlite3_uint64>(value.size()), SQLITE_TRANSIENT, SQLITE_UTF8));
    }
    int step() { return sqlite3_step(statement_); }
    void reset() {
        require(sqlite3_reset(statement_));
        require(sqlite3_clear_bindings(statement_));
    }
    std::string text(const int column) const {
        const auto* raw = sqlite3_column_text(statement_, column);
        if (raw == nullptr) throw SqliteError{SQLITE_MISMATCH, "NULL sample registry value"};
        return {reinterpret_cast<const char*>(raw),
                static_cast<std::size_t>(sqlite3_column_bytes(statement_, column))};
    }
private:
    void require(const int result) const {
        if (result != SQLITE_OK) {
            throw SqliteError{result,
                std::string{"Project sample registry: "} + sqlite3_errmsg(database_)};
        }
    }
    sqlite3* database_;
    sqlite3_stmt* statement_{nullptr};
};

[[nodiscard]] bool owns_project(sqlite3* database, const std::string_view project_id) {
    Statement query{database,
        "SELECT 1 FROM project_metadata WHERE singleton=1 AND project_id=?;"};
    query.bind(1, project_id);
    const int result = query.step();
    if (result == SQLITE_ROW) return true;
    if (result == SQLITE_DONE) return false;
    throw SqliteError{result, std::string{"Unable to verify sample project owner: "} +
        sqlite3_errmsg(database)};
}

}  // namespace

SqliteProjectSampleStore::SqliteProjectSampleStore(SqliteConnection& connection) noexcept
    : connection_{connection} {}

std::optional<std::vector<domain::ProjectSample>> SqliteProjectSampleStore::list(
    const std::string_view project_id) {
    sqlite3* const database = connection_.native_handle();
    if (!owns_project(database, project_id)) return std::nullopt;

    Statement query{database,
        "SELECT project_id, sample_id, display_name, group_label "
        "FROM project_samples WHERE project_id=? ORDER BY sample_id COLLATE BINARY;"};
    query.bind(1, project_id);

    std::vector<domain::ProjectSample> samples;
    for (;;) {
        const int result = query.step();
        if (result == SQLITE_DONE) return samples;
        if (result != SQLITE_ROW) {
            throw SqliteError{result, std::string{"Unable to list project samples: "} +
                sqlite3_errmsg(database)};
        }
        samples.emplace_back(query.text(0), query.text(1), query.text(2), query.text(3));
    }
}

application::SampleBatchAddResult SqliteProjectSampleStore::add_batch(
    const std::string_view project_id,
    const std::span<const domain::ProjectSample> samples) {
    Transaction transaction{connection_};
    sqlite3* const database = connection_.native_handle();
    if (!owns_project(database, project_id)) {
        return application::SampleBatchAddResult::project_not_found;
    }

    std::unordered_set<std::string> batch_ids;
    batch_ids.reserve(samples.size());
    Statement exists{database,
        "SELECT 1 FROM project_samples WHERE project_id=? AND sample_id=?;"};

    for (const auto& sample : samples) {
        if (sample.project_id() != project_id) {
            throw std::invalid_argument{"Sample batch contains a different project id"};
        }
        if (!batch_ids.emplace(sample.sample_id()).second) {
            return application::SampleBatchAddResult::duplicate_sample;
        }

        exists.bind(1, project_id);
        exists.bind(2, sample.sample_id());
        const int result = exists.step();
        if (result == SQLITE_ROW) {
            return application::SampleBatchAddResult::duplicate_sample;
        }
        if (result != SQLITE_DONE) {
            throw SqliteError{result, std::string{"Unable to inspect sample duplicate: "} +
                sqlite3_errmsg(database)};
        }
        exists.reset();
    }

    Statement insert{database,
        "INSERT INTO project_samples(project_id,sample_id,display_name,group_label) "
        "VALUES(?,?,?,?);"};
    for (const auto& sample : samples) {
        insert.bind(1, project_id);
        insert.bind(2, sample.sample_id());
        insert.bind(3, sample.display_name());
        insert.bind(4, sample.group_label());
        const int result = insert.step();
        if (result != SQLITE_DONE) {
            throw SqliteError{result, std::string{"Unable to insert project sample: "} +
                sqlite3_errmsg(database)};
        }
        insert.reset();
    }

    transaction.commit();
    return application::SampleBatchAddResult::added;
}

}  // namespace biocore::infrastructure::sqlite
