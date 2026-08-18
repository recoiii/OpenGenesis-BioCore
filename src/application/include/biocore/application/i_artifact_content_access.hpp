#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "biocore/application/generated_output_artifact.hpp"

namespace biocore::application {

enum class ArtifactContentStatus {
    verified,
    missing,
    unsafe_path,
    not_regular,
    size_mismatch,
    checksum_unavailable,
    checksum_mismatch,
    io_error
};

struct ArtifactContentVerification final {
    ArtifactContentStatus status{ArtifactContentStatus::io_error};
    std::optional<std::string> content_path;
    std::optional<std::string> computed_sha256;
    std::int64_t actual_size_bytes{0};
};

class IArtifactContentAccess {
public:
    virtual ~IArtifactContentAccess() = default;

    [[nodiscard]] virtual ArtifactContentVerification verify_for_download(
        const GeneratedOutputArtifact& artifact
    ) = 0;
};

}  // namespace biocore::application
