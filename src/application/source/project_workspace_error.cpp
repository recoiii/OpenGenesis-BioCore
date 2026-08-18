#include "biocore/application/project_workspace_error.hpp"

#include <utility>

namespace biocore::application {

ProjectWorkspaceConflictError::ProjectWorkspaceConflictError(std::string entry_name)
    : std::runtime_error{"The project root already contains a reserved OpenGenesis-BioCore entry: " + entry_name} {}

ProjectWorkspaceInitializationError::ProjectWorkspaceInitializationError(std::string message)
    : std::runtime_error{std::move(message)} {}

}  // namespace biocore::application
