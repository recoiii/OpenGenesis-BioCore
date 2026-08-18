#pragma once

#include <stdexcept>
#include <string>

namespace biocore::application {

class ProjectWorkspaceConflictError final : public std::runtime_error {
public:
    explicit ProjectWorkspaceConflictError(std::string entry_name);
};

class ProjectWorkspaceInitializationError final : public std::runtime_error {
public:
    explicit ProjectWorkspaceInitializationError(std::string message);
};

}  // namespace biocore::application
