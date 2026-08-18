#pragma once

#include <stdexcept>
#include <string>

namespace biocore::application {

enum class ArtifactPresentationErrorCode {
    job_not_found,
    artifact_not_found,
    content_missing,
    unsafe_content,
    content_not_regular,
    size_mismatch,
    checksum_unavailable,
    checksum_mismatch,
    content_io_error
};

class ArtifactPresentationError final : public std::runtime_error {
public:
    ArtifactPresentationError(ArtifactPresentationErrorCode code, std::string message);
    [[nodiscard]] ArtifactPresentationErrorCode code() const noexcept;

private:
    ArtifactPresentationErrorCode code_;
};

}  // namespace biocore::application
