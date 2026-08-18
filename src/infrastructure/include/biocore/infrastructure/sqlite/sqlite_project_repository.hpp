#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "biocore/application/i_project_repository.hpp"

namespace biocore::infrastructure::sqlite {

class SqliteConnection;

class SqliteProjectRepository final : public application::IProjectRepository {
public:
    explicit SqliteProjectRepository(SqliteConnection& connection) noexcept;

    void save(const domain::Project& project) override;
    [[nodiscard]] std::optional<domain::Project> find_by_id(std::string_view project_id) override;
    [[nodiscard]] std::optional<domain::Project> find_by_root_path(std::string_view root_path) override;
    [[nodiscard]] std::vector<domain::Project> list() override;
    bool remove(std::string_view project_id) override;

private:
    SqliteConnection& connection_;
};

}  // namespace biocore::infrastructure::sqlite
