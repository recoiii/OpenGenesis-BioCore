#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"

#include <sqlite3.h>

#include <string>
#include <utility>

#include "biocore/infrastructure/sqlite/sqlite_error.hpp"

namespace biocore::infrastructure::sqlite {
namespace {

[[nodiscard]] std::string database_error_message(sqlite3* const database, const std::string_view prefix) {
    std::string message{prefix};
    if (database != nullptr) {
        message += ": ";
        message += sqlite3_errmsg(database);
    }
    return message;
}

}  // namespace

SqliteConnection::SqliteConnection(const std::filesystem::path& database_path) {
    const std::u8string utf8_path = database_path.u8string();
    const std::string path{
        reinterpret_cast<const char*>(utf8_path.data()),
        utf8_path.size(),
    };
    constexpr int open_flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;

    const int result = sqlite3_open_v2(path.c_str(), &database_, open_flags, nullptr);
    if (result != SQLITE_OK) {
        const std::string message = database_error_message(database_, "Unable to open SQLite database");
        if (database_ != nullptr) {
            sqlite3_close_v2(database_);
            database_ = nullptr;
        }
        throw SqliteError{result, message};
    }

    sqlite3_extended_result_codes(database_, 1);

    try {
        execute("PRAGMA foreign_keys = ON;");
        execute("PRAGMA busy_timeout = 5000;");
        execute("PRAGMA journal_mode = WAL;");
    } catch (...) {
        sqlite3_close_v2(database_);
        database_ = nullptr;
        throw;
    }
}

SqliteConnection::~SqliteConnection() {
    if (database_ != nullptr) {
        sqlite3_close_v2(database_);
    }
}

void SqliteConnection::execute(const std::string_view sql) {
    char* raw_error_message = nullptr;
    const std::string sql_text{sql};
    const int result = sqlite3_exec(database_, sql_text.c_str(), nullptr, nullptr, &raw_error_message);

    if (result != SQLITE_OK) {
        std::string message{"SQLite statement failed"};
        if (raw_error_message != nullptr) {
            message += ": ";
            message += raw_error_message;
        }
        sqlite3_free(raw_error_message);
        throw SqliteError{result, std::move(message)};
    }
}

sqlite3* SqliteConnection::native_handle() noexcept {
    return database_;
}

const sqlite3* SqliteConnection::native_handle() const noexcept {
    return database_;
}

}  // namespace biocore::infrastructure::sqlite
