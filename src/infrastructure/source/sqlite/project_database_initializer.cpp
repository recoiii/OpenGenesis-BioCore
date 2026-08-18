#include "biocore/infrastructure/sqlite/project_database_initializer.hpp"

#include <sqlite3.h>

#include <string>
#include <string_view>

#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
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

class Statement final {
public:
    Statement(sqlite3* database, const char* sql) : database_{database} {
        const int result = sqlite3_prepare_v2(database_, sql, -1, &statement_, nullptr);
        if (result != SQLITE_OK) {
            throw SqliteError{
                result,
                std::string{"Unable to prepare project metadata statement: "} +
                    sqlite3_errmsg(database_),
            };
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
        const int result = sqlite3_bind_text(
            statement_,
            index,
            value.data(),
            static_cast<int>(value.size()),
            SQLITE_TRANSIENT
        );
        if (result != SQLITE_OK) {
            throw SqliteError{
                result,
                std::string{"Unable to bind project metadata: "} + sqlite3_errmsg(database_),
            };
        }
    }

    void require_done() {
        const int result = sqlite3_step(statement_);
        if (result != SQLITE_DONE) {
            throw SqliteError{
                result,
                std::string{"Unable to initialize project metadata: "} + sqlite3_errmsg(database_),
            };
        }
    }

private:
    sqlite3* database_;
    sqlite3_stmt* statement_{nullptr};
};

}  // namespace

ProjectDatabaseInitializer::ProjectDatabaseInitializer(SqliteConnection& connection) noexcept
    : connection_{connection} {}

void ProjectDatabaseInitializer::initialize(const domain::Project& project) {
    ProjectMigrationRunner migrations{connection_};
    migrations.apply_pending();

    constexpr const char* sql = R"sql(
        INSERT INTO project_metadata(
            singleton,
            project_id,
            name,
            root_path,
            created_at_utc,
            updated_at_utc
        ) VALUES (1, ?, ?, ?, ?, ?);
    )sql";

    Transaction transaction{connection_};
    Statement statement{connection_.native_handle(), sql};
    statement.bind_text(1, project.id());
    statement.bind_text(2, project.name());
    statement.bind_text(3, project.root_path());
    statement.bind_text(4, project.created_at_utc());
    statement.bind_text(5, project.updated_at_utc());
    statement.require_done();
    transaction.commit();
}

}  // namespace biocore::infrastructure::sqlite
