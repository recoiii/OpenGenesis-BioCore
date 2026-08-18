#include "biocore/application/artifact_presentation_service_error.hpp"

#include <utility>

namespace biocore::application {

ArtifactPresentationError::ArtifactPresentationError(
    const ArtifactPresentationErrorCode code,
    std::string message
)
    : std::runtime_error{std::move(message)}, code_{code} {}

ArtifactPresentationErrorCode ArtifactPresentationError::code() const noexcept { return code_; }

}  // namespace biocore::application
