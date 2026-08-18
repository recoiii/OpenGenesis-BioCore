#include "biocore/infrastructure/sqlite/sqlite_project_repository.hpp"

#include <sqlite3.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/domain/project.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_error.hpp"

namespace biocore::infrastructure::sqlite {
namespace {

class Statement final {
public:
    Statement(sqlite3* const database, const char* const sql) : database_{database} {
        const int result = sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr);
        if (result != SQLITE_OK) {
            throw SqliteError{result, std::string{"Unable to prepare SQLite statement: "} + sqlite3_errmsg(database_)};
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
        const auto size = static_cast<sqlite3_uint64>(value.size());
        const int result = sqlite3_bind_text64(statement_, index, value.data(), size, SQLITE_TRANSIENT, SQLITE_UTF8);
        if (result != SQLITE_OK) {
            throw SqliteError{result, std::string{"Unable to bind SQLite text value: "} + sqlite3_errmsg(database_)};
        }
    }

    [[nodiscard]] int step() {
        return sqlite3_step(statement_);
    }

    [[nodiscard]] std::string column_text(const int column) const {
        const auto* value = sqlite3_column_text(statement_, column);
        if (value == nullptr) {
            throw SqliteError{SQLITE_MISMATCH, "Unexpected NULL value in project record"};
        }

        const int byte_count = sqlite3_column_bytes(statement_, column);
        return {reinterpret_cast<const char*>(value), static_cast<std::size_t>(byte_count)};
    }

private:
    sqlite3* database_;
    sqlite3_stmt* statement_{nullptr};
};

void require_done(sqlite3* const database, Statement& statement, const std::string_view operation) {
    const int result = statement.step();
    if (result != SQLITE_DONE) {
        throw SqliteError{result, std::string{operation} + ": " + sqlite3_errmsg(database)};
    }
}

[[nodiscard]] domain::Project read_project(const Statement& statement) {
    return domain::Project{
        statement.column_text(0),
        statement.column_text(1),
        statement.column_text(2),
        statement.column_text(3),
        statement.column_text(4),
    };
}

}  // namespace

SqliteProjectRepository::SqliteProjectRepository(SqliteConnection& connection) noexcept : connection_{connection} {}

void SqliteProjectRepository::save(const domain::Project& project) {
    constexpr const char* sql = R"sql(
        INSERT INTO catalog_projects(id, name, root_path, created_at_utc, updated_at_utc)
        VALUES (?, ?, ?, ?, ?)
        ON CONFLICT(id) DO UPDATE SET
            name = excluded.name,
            root_path = excluded.root_path,
            updated_at_utc = excluded.updated_at_utc;
    )sql";

    sqlite3* const database = connection_.native_handle();
    Statement statement{database, sql};
    statement.bind_text(1, project.id());
    statement.bind_text(2, project.name());
    statement.bind_text(3, project.root_path());
    statement.bind_text(4, project.created_at_utc());
    statement.bind_text(5, project.updated_at_utc());
    require_done(database, statement, "Unable to save project");
}

std::optional<domain::Project> SqliteProjectRepository::find_by_id(const std::string_view project_id) {
    constexpr const char* sql = R"sql(
        SELECT id, name, root_path, created_at_utc, updated_at_utc
        FROM catalog_projects
        WHERE id = ?;
    )sql";

    sqlite3* const database = connection_.native_handle();
    Statement statement{database, sql};
    statement.bind_text(1, project_id);

    const int result = statement.step();
    if (result == SQLITE_DONE) {
        return std::nullopt;
    }
    if (result != SQLITE_ROW) {
        throw SqliteError{result, std::string{"Unable to find project: "} + sqlite3_errmsg(database)};
    }

    return read_project(statement);
}

std::optional<domain::Project> SqliteProjectRepository::find_by_root_path(const std::string_view root_path) {
    constexpr const char* sql = R"sql(
        SELECT id, name, root_path, created_at_utc, updated_at_utc
        FROM catalog_projects
        WHERE root_path = ?;
    )sql";

    sqlite3* const database = connection_.native_handle();
    Statement statement{database, sql};
    statement.bind_text(1, root_path);

    const int result = statement.step();
    if (result == SQLITE_DONE) {
        return std::nullopt;
    }
    if (result != SQLITE_ROW) {
        throw SqliteError{result, std::string{"Unable to find project by root path: "} + sqlite3_errmsg(database)};
    }

    return read_project(statement);
}

std::vector<domain::Project> SqliteProjectRepository::list() {
    constexpr const char* sql = R"sql(
        SELECT id, name, root_path, created_at_utc, updated_at_utc
        FROM catalog_projects
        ORDER BY created_at_utc ASC, id ASC;
    )sql";

    sqlite3* const database = connection_.native_handle();
    Statement statement{database, sql};
    std::vector<domain::Project> projects;

    while (true) {
        const int result = statement.step();
        if (result == SQLITE_DONE) {
            return projects;
        }
        if (result != SQLITE_ROW) {
            throw SqliteError{result, std::string{"Unable to list projects: "} + sqlite3_errmsg(database)};
        }
        projects.push_back(read_project(statement));
    }
}

bool SqliteProjectRepository::remove(const std::string_view project_id) {
    constexpr const char* sql = "DELETE FROM catalog_projects WHERE id = ?;";

    sqlite3* const database = connection_.native_handle();
    Statement statement{database, sql};
    statement.bind_text(1, project_id);
    require_done(database, statement, "Unable to remove project");
    return sqlite3_changes(database) > 0;
}

}  // namespace biocore::infrastructure::sqlite
