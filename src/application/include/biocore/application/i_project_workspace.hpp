#pragma once

#include <memory>

#include "biocore/domain/project.hpp"

namespace biocore::application {

class IProjectWorkspaceTransaction {
public:
    virtual ~IProjectWorkspaceTransaction() = default;

    virtual void commit() noexcept = 0;
};

class IProjectWorkspace {
public:
    virtual ~IProjectWorkspace() = default;

    // Initializes the reserved OpenGenesis-BioCore workspace entries for a project.
    // Destruction of the returned transaction rolls back uncommitted artifacts.
    [[nodiscard]] virtual std::unique_ptr<IProjectWorkspaceTransaction> initialize(
        const domain::Project& project
    ) = 0;
};

}  // namespace biocore::application
