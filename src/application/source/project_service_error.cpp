#include "biocore/application/project_service_error.hpp"

namespace biocore::application {

DuplicateProjectRootError::DuplicateProjectRootError()
    : std::runtime_error{"A project with the same canonical root directory is already registered"} {}

ProjectIdGenerationError::ProjectIdGenerationError()
    : std::runtime_error{"Unable to generate a unique project identifier"} {}

}  // namespace biocore::application
