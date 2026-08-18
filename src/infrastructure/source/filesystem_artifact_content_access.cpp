#include "biocore/infrastructure/filesystem_artifact_content_access.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

#include "biocore/domain/storage_mode.hpp"
#include "biocore/infrastructure/sha256.hpp"

namespace biocore::infrastructure {
namespace {
namespace fs = std::filesystem;

[[nodiscard]] fs::path path_from_utf8(const std::string_view value) {
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const char character : value) {
        utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
    }
    return fs::path{utf8};
}

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const std::u8string utf8 = path.generic_u8string();
    return std::string{reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

[[nodiscard]] fs::path require_real_directory(
    const fs::path& input,
    const std::string_view description
) {
    std::error_code error;
    const auto status = fs::symlink_status(input, error);
    if (error || fs::is_symlink(status)) {
        throw std::invalid_argument(std::string{description} + " must not be a symbolic link");
    }
    const auto canonical = fs::canonical(input, error);
    if (error || input.lexically_normal() != canonical || !fs::is_directory(canonical, error) || error) {
        throw std::invalid_argument(std::string{description} + " must be a canonical directory");
    }
    return canonical;
}

[[nodiscard]] bool valid_sha256(const std::optional<std::string>& algorithm,
                                const std::optional<std::string>& value) {
    if (!algorithm.has_value() || !value.has_value() || *algorithm != "sha256" ||
        value->size() != 64U) {
        return false;
    }
    return std::ranges::all_of(*value, [](const char character) {
        return (character >= '0' && character <= '9') ||
               (character >= 'a' && character <= 'f');
    });
}

[[nodiscard]] std::optional<fs::path> safe_flat_output_relative_path(
    const std::optional<std::string>& value
) {
    if (!value.has_value() || value->empty() || value->size() > 4096U ||
        value->find('\0') != std::string::npos) {
        return std::nullopt;
    }
    const auto path = path_from_utf8(*value);
    if (path.is_absolute() || path.has_root_path() || path.lexically_normal() != path) {
        return std::nullopt;
    }
    auto iterator = path.begin();
    if (iterator == path.end() || *iterator != "outputs") {
        return std::nullopt;
    }
    ++iterator;
    if (iterator == path.end() || *iterator == "." || *iterator == "..") {
        return std::nullopt;
    }
    ++iterator;
    if (iterator != path.end()) {
        return std::nullopt;
    }
    return path;
}

[[nodiscard]] application::ArtifactContentVerification status_only(
    const application::ArtifactContentStatus status
) {
    return {
        .status = status,
        .content_path = std::nullopt,
        .computed_sha256 = std::nullopt,
        .actual_size_bytes = 0,
    };
}

}  // namespace

FilesystemArtifactContentAccess::FilesystemArtifactContentAccess(fs::path project_root)
    : project_root_{require_real_directory(project_root, "Artifact project root")},
      outputs_directory_{require_real_directory(project_root_ / "outputs", "Artifact outputs directory")} {}

application::ArtifactContentVerification FilesystemArtifactContentAccess::verify_for_download(
    const application::GeneratedOutputArtifact& artifact
) {
    using application::ArtifactContentStatus;

    if (artifact.file.storage_mode() != domain::StorageMode::generated_output ||
        !artifact.file.managed_path().has_value()) {
        return status_only(ArtifactContentStatus::unsafe_path);
    }
    if (!valid_sha256(artifact.file.checksum_algorithm(), artifact.file.checksum_value())) {
        return status_only(ArtifactContentStatus::checksum_unavailable);
    }
    const auto relative = safe_flat_output_relative_path(artifact.file.relative_project_path());
    if (!relative.has_value() || artifact.provenance.relative_project_path !=
                                     path_to_utf8(*relative)) {
        return status_only(ArtifactContentStatus::unsafe_path);
    }

    const auto candidate = project_root_ / *relative;
    if (candidate.parent_path() != outputs_directory_) {
        return status_only(ArtifactContentStatus::unsafe_path);
    }

    std::error_code error;
    const auto status = fs::symlink_status(candidate, error);
    if (error) {
        return status_only(ArtifactContentStatus::io_error);
    }
    if (status.type() == fs::file_type::not_found) {
        return status_only(ArtifactContentStatus::missing);
    }
    if (fs::is_symlink(status)) {
        return status_only(ArtifactContentStatus::unsafe_path);
    }
    if (!fs::is_regular_file(status)) {
        return status_only(ArtifactContentStatus::not_regular);
    }

    error.clear();
    const auto canonical = fs::canonical(candidate, error);
    if (error || canonical != candidate.lexically_normal() ||
        canonical.parent_path() != outputs_directory_ ||
        path_to_utf8(canonical) != *artifact.file.managed_path()) {
        return status_only(ArtifactContentStatus::unsafe_path);
    }

    error.clear();
    const auto size_before = fs::file_size(canonical, error);
    if (error || size_before > static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max())) {
        return status_only(ArtifactContentStatus::io_error);
    }
    if (static_cast<std::int64_t>(size_before) != artifact.file.size_bytes()) {
        return {
            .status = ArtifactContentStatus::size_mismatch,
            .content_path = std::nullopt,
            .computed_sha256 = std::nullopt,
            .actual_size_bytes = static_cast<std::int64_t>(size_before),
        };
    }

    error.clear();
    const auto modified_before = fs::last_write_time(canonical, error);
    if (error) {
        return status_only(ArtifactContentStatus::io_error);
    }

    std::string checksum;
    try {
        checksum = sha256_file_hex(canonical);
    } catch (...) {
        return status_only(ArtifactContentStatus::io_error);
    }

    error.clear();
    const auto status_after = fs::symlink_status(candidate, error);
    if (error || fs::is_symlink(status_after) || !fs::is_regular_file(status_after)) {
        return status_only(ArtifactContentStatus::unsafe_path);
    }
    error.clear();
    const auto canonical_after = fs::canonical(candidate, error);
    if (error || canonical_after != canonical || canonical_after.parent_path() != outputs_directory_) {
        return status_only(ArtifactContentStatus::unsafe_path);
    }
    error.clear();
    const auto size_after = fs::file_size(canonical_after, error);
    if (error || size_after != size_before) {
        return status_only(ArtifactContentStatus::io_error);
    }
    error.clear();
    const auto modified_after = fs::last_write_time(canonical_after, error);
    if (error || modified_after != modified_before) {
        return status_only(ArtifactContentStatus::io_error);
    }
    if (checksum != *artifact.file.checksum_value()) {
        return {
            .status = ArtifactContentStatus::checksum_mismatch,
            .content_path = std::nullopt,
            .computed_sha256 = checksum,
            .actual_size_bytes = static_cast<std::int64_t>(size_after),
        };
    }

    return {
        .status = ArtifactContentStatus::verified,
        .content_path = path_to_utf8(canonical_after),
        .computed_sha256 = std::move(checksum),
        .actual_size_bytes = static_cast<std::int64_t>(size_after),
    };
}

}  // namespace biocore::infrastructure
