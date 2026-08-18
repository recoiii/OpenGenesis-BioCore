#include "biocore/infrastructure/filesystem_output_artifact_inspector.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include "biocore/infrastructure/sha256.hpp"

namespace biocore::infrastructure {
namespace {

[[nodiscard]] std::filesystem::path path_from_utf8(const std::string_view value) {
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const char character : value) {
        utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
    }
    return std::filesystem::path{utf8};
}

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string utf8 = path.generic_u8string();
    return std::string{reinterpret_cast<const char*>(utf8.data()), utf8.size()};
}

[[nodiscard]] std::filesystem::path require_real_directory(
    const std::filesystem::path& input,
    const std::string_view description
) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(input, error);
    if (error || std::filesystem::is_symlink(status)) {
        throw std::invalid_argument(std::string{description} + " must not be a symbolic link");
    }
    const auto canonical = std::filesystem::canonical(input, error);
    if (error || input.lexically_normal() != canonical ||
        !std::filesystem::is_directory(canonical, error) || error) {
        throw std::invalid_argument(std::string{description} + " must be a canonical directory");
    }
    return canonical;
}

[[nodiscard]] std::filesystem::path safe_flat_output_relative_path(
    const std::string_view value
) {
    if (value.empty() || value.size() > 4096U || value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("Generated output relative path is invalid");
    }
    const auto path = path_from_utf8(value);
    if (path.is_absolute() || path.has_root_path() || path.lexically_normal() != path) {
        throw std::invalid_argument("Generated output path must be normalized and project-relative");
    }
    auto iterator = path.begin();
    if (iterator == path.end() || *iterator != "outputs") {
        throw std::invalid_argument("Generated output must be inside the outputs directory");
    }
    ++iterator;
    if (iterator == path.end() || *iterator == "." || *iterator == "..") {
        throw std::invalid_argument("Generated output must have a file name");
    }
    ++iterator;
    if (iterator != path.end()) {
        throw std::invalid_argument("Generated output must use the flat OpenGenesis-BioCore outputs namespace");
    }
    return path;
}

}  // namespace

FilesystemOutputArtifactInspector::FilesystemOutputArtifactInspector(
    std::filesystem::path project_root
)
    : project_root_{require_real_directory(project_root, "Artifact project root")},
      outputs_directory_{require_real_directory(
          project_root_ / "outputs", "Artifact outputs directory"
      )} {}

application::InspectedOutputArtifact
FilesystemOutputArtifactInspector::inspect_existing_output(
    const std::string_view relative_project_path
) {
    const auto relative = safe_flat_output_relative_path(relative_project_path);
    const auto candidate = project_root_ / relative;
    if (candidate.parent_path() != outputs_directory_) {
        throw std::invalid_argument("Generated output parent is not the project outputs directory");
    }

    std::error_code error;
    const auto status = std::filesystem::symlink_status(candidate, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status)) {
        throw std::invalid_argument("Generated output must be an existing non-symlink regular file");
    }
    const auto canonical = std::filesystem::canonical(candidate, error);
    if (error || canonical != candidate.lexically_normal() ||
        canonical.parent_path() != outputs_directory_) {
        throw std::invalid_argument("Generated output must resolve to the expected project-local path");
    }
    const auto size = std::filesystem::file_size(canonical, error);
    if (error || size > static_cast<std::uintmax_t>(std::numeric_limits<std::int64_t>::max())) {
        throw std::runtime_error("Generated output size cannot be represented safely");
    }
    error.clear();
    const auto modified_before = std::filesystem::last_write_time(canonical, error);
    if (error) {
        throw std::runtime_error("Generated output modification time cannot be read safely");
    }

    const std::string checksum = sha256_file_hex(canonical);

    error.clear();
    const auto status_after = std::filesystem::symlink_status(candidate, error);
    if (error || std::filesystem::is_symlink(status_after) ||
        !std::filesystem::is_regular_file(status_after)) {
        throw std::runtime_error("Generated output changed during integrity inspection");
    }
    error.clear();
    const auto canonical_after = std::filesystem::canonical(candidate, error);
    if (error || canonical_after != canonical || canonical_after.parent_path() != outputs_directory_) {
        throw std::runtime_error("Generated output path changed during integrity inspection");
    }
    error.clear();
    const auto size_after = std::filesystem::file_size(canonical_after, error);
    if (error || size_after != size) {
        throw std::runtime_error("Generated output size changed during integrity inspection");
    }
    error.clear();
    const auto modified_after = std::filesystem::last_write_time(canonical_after, error);
    if (error || modified_after != modified_before) {
        throw std::runtime_error("Generated output modification time changed during integrity inspection");
    }

    const std::string display_name = path_to_utf8(canonical.filename());
    if (display_name.empty()) {
        throw std::invalid_argument("Generated output file name is invalid");
    }
    return application::InspectedOutputArtifact{
        .display_name = display_name,
        .managed_path = path_to_utf8(canonical),
        .relative_project_path = path_to_utf8(relative),
        .size_bytes = static_cast<std::int64_t>(size),
        .modified_at_utc = std::nullopt,
        .checksum_algorithm = "sha256",
        .checksum_value = checksum,
    };
}

}  // namespace biocore::infrastructure
