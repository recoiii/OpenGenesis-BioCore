#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "biocore/application/generated_output_artifact.hpp"

namespace biocore::application {

enum class ResultArtifactReadStatus {
    verified,
    too_large,
    missing,
    unsafe_path,
    not_regular,
    size_mismatch,
    checksum_unavailable,
    checksum_mismatch,
    io_error
};

struct ResultArtifactText final {
    ResultArtifactReadStatus status{ResultArtifactReadStatus::io_error};
    std::optional<std::string> text;
    std::optional<std::string> verified_sha256;
};

class IResultArtifactReader {
public:
    virtual ~IResultArtifactReader() = default;

    [[nodiscard]] virtual ResultArtifactText read_verified_text(
        const GeneratedOutputArtifact& artifact,
        std::size_t maximum_bytes
    ) = 0;
};

}  // namespace biocore::application
