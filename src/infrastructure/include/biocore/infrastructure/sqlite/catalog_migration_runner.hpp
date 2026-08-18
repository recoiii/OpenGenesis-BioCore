#pragma once

#include <cstdint>

namespace biocore::infrastructure::sqlite {

class SqliteConnection;

inline constexpr std::int32_t latest_catalog_schema_version = 1;

class CatalogMigrationRunner final {
public:
    explicit CatalogMigrationRunner(SqliteConnection& connection) noexcept;

    void apply_pending();
    [[nodiscard]] std::int32_t current_version() const;

private:
    SqliteConnection& connection_;
};

}  // namespace biocore::infrastructure::sqlite
