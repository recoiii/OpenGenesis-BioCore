#include "biocore/application/output_artifact_service_error.hpp"

#include <utility>

namespace biocore::application {

OutputArtifactServiceError::OutputArtifactServiceError(
    const OutputArtifactServiceErrorCode code,
    std::string message
)
    : std::runtime_error{std::move(message)}, code_{code} {}

OutputArtifactServiceErrorCode OutputArtifactServiceError::code() const noexcept { return code_; }

}  // namespace biocore::application
