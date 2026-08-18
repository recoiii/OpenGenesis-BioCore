#pragma once

#include <memory>

#include "biocore/application/i_project_workspace.hpp"

namespace biocore::infrastructure {

class FilesystemProjectWorkspace final : public application::IProjectWorkspace {
public:
    static constexpr int ownership_schema_version = 1;

    [[nodiscard]] std::unique_ptr<application::IProjectWorkspaceTransaction> initialize(
        const domain::Project& project
    ) override;
};

}  // namespace biocore::infrastructure
