#pragma once

#include <stdexcept>
#include <string>

namespace biocore::infrastructure::sqlite {

class SqliteError final : public std::runtime_error {
public:
    SqliteError(int result_code, std::string message);

    [[nodiscard]] int result_code() const noexcept;
    [[nodiscard]] bool is_constraint_violation() const noexcept;

private:
    int result_code_;
};

}  // namespace biocore::infrastructure::sqlite
