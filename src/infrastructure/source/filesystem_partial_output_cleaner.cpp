#include "biocore/infrastructure/filesystem_partial_output_cleaner.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>

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

[[nodiscard]] std::filesystem::path require_safe_component(
    const std::string_view value,
    const std::string_view description
) {
    if (value.empty() || value.size() > 128U || value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string{description} + " is invalid");
    }
    const auto path = path_from_utf8(value);
    if (path.is_absolute() || path.has_root_path() || path.lexically_normal() != path ||
        path == "." || path == ".." || std::distance(path.begin(), path.end()) != 1) {
        throw std::invalid_argument(std::string{description} + " must be one safe path component");
    }
    return path;
}

[[nodiscard]] std::string safe_flat_output_relative_path(const std::string_view value) {
    if (value.empty() || value.size() > 4096U || value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("Protected output path is invalid");
    }
    const auto path = path_from_utf8(value);
    if (path.is_absolute() || path.has_root_path() || path.lexically_normal() != path) {
        throw std::invalid_argument("Protected output path must be normalized and project-relative");
    }
    auto iterator = path.begin();
    if (iterator == path.end() || *iterator != "outputs") {
        throw std::invalid_argument("Protected output path must be inside outputs");
    }
    ++iterator;
    if (iterator == path.end() || *iterator == "." || *iterator == "..") {
        throw std::invalid_argument("Protected output path must have a file name");
    }
    ++iterator;
    if (iterator != path.end()) {
        throw std::invalid_argument("Protected output path must use the flat outputs namespace");
    }
    return path_to_utf8(path);
}

[[nodiscard]] std::filesystem::path ensure_child_directory(
    const std::filesystem::path& parent,
    const std::filesystem::path& component,
    const std::string_view description
) {
    const auto candidate = parent / component;
    std::error_code error;
    auto status = std::filesystem::symlink_status(candidate, error);
    if (error && error != std::errc::no_such_file_or_directory) {
        throw std::runtime_error(std::string{"Unable to inspect "} + std::string{description});
    }
    if (!std::filesystem::exists(status)) {
        error.clear();
        if (!std::filesystem::create_directory(candidate, error) || error) {
            throw std::runtime_error(std::string{"Unable to create "} + std::string{description});
        }
        status = std::filesystem::symlink_status(candidate, error);
    }
    if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_directory(status)) {
        throw std::invalid_argument(std::string{description} + " must be a non-symlink directory");
    }
    const auto canonical = std::filesystem::canonical(candidate, error);
    if (error || canonical != candidate.lexically_normal() || canonical.parent_path() != parent) {
        throw std::invalid_argument(std::string{description} + " must remain project-local");
    }
    return canonical;
}

[[nodiscard]] std::filesystem::path unique_quarantine_destination(
    const std::filesystem::path& directory,
    const std::filesystem::path& filename
) {
    std::filesystem::path base = filename;
    base += ".partial";
    for (std::size_t attempt = 0U; attempt < 1'000U; ++attempt) {
        std::filesystem::path candidate = directory / base;
        if (attempt != 0U) {
            candidate += "." + std::to_string(attempt);
        }
        std::error_code error;
        const auto status = std::filesystem::symlink_status(candidate, error);
        if (error == std::errc::no_such_file_or_directory || !std::filesystem::exists(status)) {
            return candidate;
        }
        if (error) {
            throw std::runtime_error("Unable to inspect quarantine destination");
        }
    }
    throw std::runtime_error("Unable to allocate a unique quarantine destination");
}

}  // namespace

FilesystemPartialOutputCleaner::FilesystemPartialOutputCleaner(
    std::filesystem::path project_root
)
    : project_root_{require_real_directory(project_root, "Cleanup project root")},
      outputs_directory_{require_real_directory(
          project_root_ / "outputs", "Cleanup outputs directory"
      )},
      metadata_directory_{require_real_directory(
          project_root_ / ".biocore", "Cleanup metadata directory"
      )} {}

application::PartialOutputCleanupResult
FilesystemPartialOutputCleaner::quarantine_unregistered_outputs(
    const std::string_view job_id,
    const std::span<const std::string> protected_relative_paths
) {
    const auto job_component = require_safe_component(job_id, "Cleanup job id");
    const std::string prefix = std::string{job_id} + "--";

    std::unordered_set<std::string> protected_paths;
    protected_paths.reserve(protected_relative_paths.size());
    for (const auto& path : protected_relative_paths) {
        protected_paths.insert(safe_flat_output_relative_path(path));
    }

    application::PartialOutputCleanupResult result;
    std::vector<std::filesystem::path> candidates;
    std::error_code error;
    for (std::filesystem::directory_iterator iterator{outputs_directory_, error}, end;
         !error && iterator != end;
         iterator.increment(error)) {
        const auto filename = path_to_utf8(iterator->path().filename());
        if (!filename.starts_with(prefix)) {
            continue;
        }
        const std::string relative = "outputs/" + filename;
        if (protected_paths.contains(relative)) {
            continue;
        }
        const auto status = iterator->symlink_status(error);
        if (error) break;
        if (std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
            result.skipped_relative_paths.push_back(relative);
            continue;
        }
        const auto canonical = std::filesystem::canonical(iterator->path(), error);
        if (error || canonical.parent_path() != outputs_directory_ ||
            canonical != iterator->path().lexically_normal()) {
            error.clear();
            result.skipped_relative_paths.push_back(relative);
            continue;
        }
        candidates.push_back(canonical);
    }
    if (error) {
        throw std::runtime_error("Unable to enumerate partial generated outputs");
    }
    if (candidates.empty()) {
        return result;
    }

    const auto quarantine = ensure_child_directory(
        metadata_directory_, "quarantine", "Cleanup quarantine directory"
    );
    const auto outputs_quarantine = ensure_child_directory(
        quarantine, "outputs", "Cleanup output quarantine directory"
    );
    const auto job_quarantine = ensure_child_directory(
        outputs_quarantine, job_component, "Cleanup job quarantine directory"
    );

    for (const auto& candidate : candidates) {
        const auto destination = unique_quarantine_destination(
            job_quarantine, candidate.filename()
        );
        error.clear();
        std::filesystem::rename(candidate, destination, error);
        if (error) {
            throw std::runtime_error("Unable to quarantine a partial generated output");
        }
        result.quarantined.push_back(application::QuarantinedPartialOutput{
            .original_relative_path = "outputs/" + path_to_utf8(candidate.filename()),
            .quarantine_relative_path = path_to_utf8(
                std::filesystem::relative(destination, project_root_)
            ),
        });
    }
    return result;
}

}  // namespace biocore::infrastructure
