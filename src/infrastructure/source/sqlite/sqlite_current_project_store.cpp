#include "biocore/infrastructure/sqlite/sqlite_current_project_store.hpp"

#include <sqlite3.h>

#include <string>

#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"

namespace biocore::infrastructure::sqlite {

SqliteCurrentProjectStore::SqliteCurrentProjectStore(SqliteConnection& connection) noexcept
    : connection_{connection} {}

std::optional<domain::Project> SqliteCurrentProjectStore::find() {
    sqlite3_stmt* statement = nullptr;
    constexpr const char* sql =
        "SELECT project_id,name,root_path,created_at_utc,updated_at_utc "
        "FROM project_metadata WHERE singleton=1;";
    const int prepare = sqlite3_prepare_v2(connection_.native_handle(), sql, -1, &statement, nullptr);
    if (prepare != SQLITE_OK) {
        throw SqliteError{prepare, std::string{"Unable to prepare current project query: "} +
                                      sqlite3_errmsg(connection_.native_handle())};
    }
    const auto finalize = [&]() noexcept { sqlite3_finalize(statement); };
    const int step = sqlite3_step(statement);
    if (step == SQLITE_DONE) {
        finalize();
        return std::nullopt;
    }
    if (step != SQLITE_ROW) {
        const std::string message = std::string{"Unable to read current project: "} +
                                    sqlite3_errmsg(connection_.native_handle());
        finalize();
        throw SqliteError{step, message};
    }
    const auto text = [&](const int column) -> std::string {
        const auto* value = sqlite3_column_text(statement, column);
        if (value == nullptr) throw SqliteError{SQLITE_CORRUPT, "Current project contains NULL text"};
        return {reinterpret_cast<const char*>(value),
                static_cast<std::size_t>(sqlite3_column_bytes(statement, column))};
    };
    domain::Project project{text(0), text(1), text(2), text(3), text(4)};
    const int next = sqlite3_step(statement);
    if (next != SQLITE_DONE) {
        finalize();
        throw SqliteError{SQLITE_CORRUPT, "Project database contains multiple singleton project rows"};
    }
    finalize();
    return project;
}

}  // namespace biocore::infrastructure::sqlite
