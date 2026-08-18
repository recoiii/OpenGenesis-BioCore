#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "biocore/domain/project.hpp"

namespace biocore::application {

class IProjectRepository {
public:
    virtual ~IProjectRepository() = default;

    // Inserts a new project or updates its mutable fields. Existing creation timestamps are preserved.
    // A failed save must not leave a partially persisted project.
    virtual void save(const domain::Project& project) = 0;
    [[nodiscard]] virtual std::optional<domain::Project> find_by_id(std::string_view project_id) = 0;
    [[nodiscard]] virtual std::optional<domain::Project> find_by_root_path(std::string_view root_path) = 0;
    [[nodiscard]] virtual std::vector<domain::Project> list() = 0;
    virtual bool remove(std::string_view project_id) = 0;
};

}  // namespace biocore::application
