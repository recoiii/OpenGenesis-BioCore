#pragma once

#include <filesystem>
#include <string_view>

struct sqlite3;

namespace biocore::infrastructure::sqlite {

class SqliteConnection final {
public:
    explicit SqliteConnection(const std::filesystem::path& database_path);
    ~SqliteConnection();

    SqliteConnection(const SqliteConnection&) = delete;
    SqliteConnection& operator=(const SqliteConnection&) = delete;
    SqliteConnection(SqliteConnection&&) = delete;
    SqliteConnection& operator=(SqliteConnection&&) = delete;

    void execute(std::string_view sql);

    [[nodiscard]] sqlite3* native_handle() noexcept;
    [[nodiscard]] const sqlite3* native_handle() const noexcept;

private:
    sqlite3* database_{nullptr};
};

}  // namespace biocore::infrastructure::sqlite
