#pragma once

#include <stdexcept>

namespace biocore::application {

class DuplicateProjectRootError final : public std::runtime_error {
public:
    DuplicateProjectRootError();
};

class ProjectIdGenerationError final : public std::runtime_error {
public:
    ProjectIdGenerationError();
};

}  // namespace biocore::application
