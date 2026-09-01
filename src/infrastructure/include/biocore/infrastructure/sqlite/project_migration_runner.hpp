#pragma once

#include <cstdint>

namespace biocore::infrastructure::sqlite {

class SqliteConnection;

inline constexpr std::int32_t latest_project_schema_version = 8;

class ProjectMigrationRunner final {
public:
    explicit ProjectMigrationRunner(SqliteConnection& connection) noexcept;

    void apply_pending();
    [[nodiscard]] std::int32_t current_version() const;

private:
    SqliteConnection& connection_;
};

}  // namespace biocore::infrastructure::sqlite
