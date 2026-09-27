#include "biocore/infrastructure/sqlite/sqlite_project_sample_binding_store.hpp"

#include <sqlite3.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"

namespace biocore::infrastructure::sqlite {
namespace {

class Statement final {
public:
    Statement(sqlite3* database, const char* sql) : database_{database} {
        require(sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr));
    }

    ~Statement() { sqlite3_finalize(statement_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    void bind(const int index, const std::string_view value) {
        require(sqlite3_bind_text64(
            statement_, index, value.data(),
            static_cast<sqlite3_uint64>(value.size()),
            SQLITE_TRANSIENT, SQLITE_UTF8
        ));
    }

    void bind_optional(const int index, const std::optional<std::string>& value) {
        if (value.has_value()) {
            bind(index, *value);
        } else {
            require(sqlite3_bind_null(statement_, index));
        }
    }

    [[nodiscard]] int step() { return sqlite3_step(statement_); }

    [[nodiscard]] std::string text(const int column) const {
        const auto* raw = sqlite3_column_text(statement_, column);
        if (raw == nullptr) {
            throw SqliteError{SQLITE_MISMATCH, "NULL sample binding text value"};
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

private:
    void require(const int result) const {
        if (result != SQLITE_OK) {
            throw SqliteError{
                result,
                std::string{"Project sample binding store: "} + sqlite3_errmsg(database_)
            };
        }
    }

    sqlite3* database_;
    sqlite3_stmt* statement_{nullptr};
};

[[nodiscard]] domain::ProjectSampleBinding row(Statement& statement) {
    return domain::ProjectSampleBinding{
        statement.text(0),
        statement.text(1),
        domain::sample_input_layout_from_string(statement.text(2)),
        statement.text(3),
        statement.optional_text(4),
        statement.optional_text(5),
    };
}

}  // namespace

SqliteProjectSampleBindingStore::SqliteProjectSampleBindingStore(
    SqliteConnection& connection
) noexcept
    : connection_{connection} {}

std::optional<domain::ProjectSampleBinding>
SqliteProjectSampleBindingStore::find(
    const std::string_view project_id,
    const std::string_view sample_id
) {
    Statement query{
        connection_.native_handle(),
        "SELECT project_id,sample_id,input_layout,primary_file_id,"
        "secondary_file_id,reference_file_id "
        "FROM project_sample_bindings WHERE project_id=? AND sample_id=?;"
    };
    query.bind(1, project_id);
    query.bind(2, sample_id);
    const int result = query.step();
    if (result == SQLITE_DONE) return std::nullopt;
    if (result != SQLITE_ROW) {
        throw SqliteError{
            result,
            std::string{"Unable to find project sample binding: "} +
                sqlite3_errmsg(connection_.native_handle())
        };
    }
    return row(query);
}

std::vector<domain::ProjectSampleBinding>
SqliteProjectSampleBindingStore::list(const std::string_view project_id) {
    Statement query{
        connection_.native_handle(),
        "SELECT project_id,sample_id,input_layout,primary_file_id,"
        "secondary_file_id,reference_file_id "
        "FROM project_sample_bindings WHERE project_id=? "
        "ORDER BY sample_id COLLATE BINARY;"
    };
    query.bind(1, project_id);

    std::vector<domain::ProjectSampleBinding> bindings;
    for (;;) {
        const int result = query.step();
        if (result == SQLITE_DONE) return bindings;
        if (result != SQLITE_ROW) {
            throw SqliteError{
                result,
                std::string{"Unable to list project sample bindings: "} +
                    sqlite3_errmsg(connection_.native_handle())
            };
        }
        bindings.push_back(row(query));
    }
}

void SqliteProjectSampleBindingStore::upsert(
    const domain::ProjectSampleBinding& binding
) {
    Statement statement{
        connection_.native_handle(),
        "INSERT INTO project_sample_bindings("
        "project_id,sample_id,input_layout,primary_file_id,secondary_file_id,reference_file_id"
        ") VALUES(?,?,?,?,?,?) "
        "ON CONFLICT(project_id,sample_id) DO UPDATE SET "
        "input_layout=excluded.input_layout,"
        "primary_file_id=excluded.primary_file_id,"
        "secondary_file_id=excluded.secondary_file_id,"
        "reference_file_id=excluded.reference_file_id;"
    };
    statement.bind(1, binding.project_id());
    statement.bind(2, binding.sample_id());
    statement.bind(3, domain::to_string(binding.layout()));
    statement.bind(4, binding.primary_file_id());
    statement.bind_optional(5, binding.secondary_file_id());
    statement.bind_optional(6, binding.reference_file_id());

    const int result = statement.step();
    if (result != SQLITE_DONE) {
        throw SqliteError{
            result,
            std::string{"Unable to persist project sample binding: "} +
                sqlite3_errmsg(connection_.native_handle())
        };
    }
}

}  // namespace biocore::infrastructure::sqlite
