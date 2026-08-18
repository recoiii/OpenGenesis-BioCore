#include "biocore/infrastructure/sqlite/sqlite_error.hpp"

#include <sqlite3.h>

#include <utility>

namespace biocore::infrastructure::sqlite {

SqliteError::SqliteError(const int result_code, std::string message)
    : std::runtime_error{std::move(message)}, result_code_{result_code} {}

int SqliteError::result_code() const noexcept {
    return result_code_;
}

bool SqliteError::is_constraint_violation() const noexcept {
    return (result_code_ & 0xFF) == SQLITE_CONSTRAINT;
}

}  // namespace biocore::infrastructure::sqlite
