#pragma once

#include <stdexcept>
#include <string>

namespace biocore::application {

enum class OutputArtifactServiceErrorCode {
    identifier_generation_exhausted,
    provenance_conflict,
    persistence_conflict
};

class OutputArtifactServiceError final : public std::runtime_error {
public:
    OutputArtifactServiceError(OutputArtifactServiceErrorCode code, std::string message);

    [[nodiscard]] OutputArtifactServiceErrorCode code() const noexcept;

private:
    OutputArtifactServiceErrorCode code_;
};

}  // namespace biocore::application
