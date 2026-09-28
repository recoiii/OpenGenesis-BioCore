#include "biocore/infrastructure/filesystem_result_artifact_reader.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <span>
#include <string>

#include "biocore/application/i_artifact_content_access.hpp"
#include "biocore/infrastructure/sha256.hpp"

namespace biocore::infrastructure {
namespace {

[[nodiscard]] application::ResultArtifactReadStatus map_status(
    const application::ArtifactContentStatus status
) noexcept {
    using application::ArtifactContentStatus;
    using application::ResultArtifactReadStatus;
    switch (status) {
        case ArtifactContentStatus::verified: return ResultArtifactReadStatus::verified;
        case ArtifactContentStatus::missing: return ResultArtifactReadStatus::missing;
        case ArtifactContentStatus::unsafe_path: return ResultArtifactReadStatus::unsafe_path;
        case ArtifactContentStatus::not_regular: return ResultArtifactReadStatus::not_regular;
        case ArtifactContentStatus::size_mismatch: return ResultArtifactReadStatus::size_mismatch;
        case ArtifactContentStatus::checksum_unavailable: return ResultArtifactReadStatus::checksum_unavailable;
        case ArtifactContentStatus::checksum_mismatch: return ResultArtifactReadStatus::checksum_mismatch;
        case ArtifactContentStatus::io_error: return ResultArtifactReadStatus::io_error;
    }
    return ResultArtifactReadStatus::io_error;
}

}  // namespace

FilesystemResultArtifactReader::FilesystemResultArtifactReader(std::filesystem::path project_root)
    : verifier_{std::move(project_root)} {}

application::ResultArtifactText FilesystemResultArtifactReader::read_verified_text(
    const application::GeneratedOutputArtifact& artifact,
    const std::size_t maximum_bytes
) {
    const auto verification = verifier_.verify_for_download(artifact);
    if (verification.status != application::ArtifactContentStatus::verified ||
        !verification.content_path.has_value() ||
        !verification.computed_sha256.has_value()) {
        return {
            .status = map_status(verification.status),
            .text = std::nullopt,
            .verified_sha256 = verification.computed_sha256,
        };
    }
    if (maximum_bytes == 0U || artifact.file.size_bytes() < 0 ||
        static_cast<std::uint64_t>(artifact.file.size_bytes()) > maximum_bytes) {
        return {
            .status = application::ResultArtifactReadStatus::too_large,
            .text = std::nullopt,
            .verified_sha256 = verification.computed_sha256,
        };
    }

    std::ifstream input{*verification.content_path, std::ios::binary};
    if (!input) {
        return {
            .status = application::ResultArtifactReadStatus::io_error,
            .text = std::nullopt,
            .verified_sha256 = verification.computed_sha256,
        };
    }
    std::string text{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}
    };
    if (input.bad() || text.size() > maximum_bytes ||
        text.size() != static_cast<std::size_t>(artifact.file.size_bytes())) {
        return {
            .status = application::ResultArtifactReadStatus::io_error,
            .text = std::nullopt,
            .verified_sha256 = verification.computed_sha256,
        };
    }
    const auto bytes = std::as_bytes(std::span{text.data(), text.size()});
    const std::string checksum = sha256_hex(bytes);
    if (checksum != verification.computed_sha256 ||
        artifact.file.checksum_value() != std::optional<std::string>{checksum}) {
        return {
            .status = application::ResultArtifactReadStatus::checksum_mismatch,
            .text = std::nullopt,
            .verified_sha256 = checksum,
        };
    }
    return {
        .status = application::ResultArtifactReadStatus::verified,
        .text = std::move(text),
        .verified_sha256 = checksum,
    };
}

}  // namespace biocore::infrastructure
