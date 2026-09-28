#pragma once

#include "biocore/application/i_current_project_store.hpp"

namespace biocore::infrastructure::sqlite {

class SqliteConnection;

class SqliteCurrentProjectStore final : public application::ICurrentProjectStore {
public:
    explicit SqliteCurrentProjectStore(SqliteConnection& connection) noexcept;
    [[nodiscard]] std::optional<domain::Project> find() override;

private:
    SqliteConnection& connection_;
};

}  // namespace biocore::infrastructure::sqlite
