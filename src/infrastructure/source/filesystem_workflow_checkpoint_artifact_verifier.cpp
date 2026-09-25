#include "biocore/infrastructure/filesystem_workflow_checkpoint_artifact_verifier.hpp"

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include "biocore/infrastructure/sha256.hpp"

namespace biocore::infrastructure {
namespace {

[[nodiscard]] bool is_safe_relative_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name() ||
        path.has_root_directory()) {
        return false;
    }
    for (const auto& component : path) {
        if (component == "..") return false;
    }
    return true;
}

[[nodiscard]] bool is_within(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate
) {
    auto root_iterator = root.begin();
    auto candidate_iterator = candidate.begin();
    for (; root_iterator != root.end(); ++root_iterator, ++candidate_iterator) {
        if (candidate_iterator == candidate.end() ||
            *root_iterator != *candidate_iterator) {
            return false;
        }
    }
    return true;
}

}  // namespace

FilesystemWorkflowCheckpointArtifactVerifier::
FilesystemWorkflowCheckpointArtifactVerifier(std::filesystem::path project_root)
    : project_root_{std::move(project_root)} {
    if (project_root_.empty()) {
        throw std::invalid_argument("Checkpoint verifier project root must not be empty");
    }
    std::error_code error;
    project_root_ = std::filesystem::weakly_canonical(project_root_, error);
    if (error || !std::filesystem::is_directory(project_root_, error) || error) {
        throw std::invalid_argument(
            "Checkpoint verifier project root must reference an existing directory"
        );
    }
}

bool FilesystemWorkflowCheckpointArtifactVerifier::verify(
    const domain::WorkflowCheckpointArtifact& artifact
) const {
    const std::filesystem::path relative{artifact.relative_project_path};
    if (!is_safe_relative_path(relative)) return false;

    std::error_code error;
    const std::filesystem::path candidate =
        std::filesystem::weakly_canonical(project_root_ / relative, error);
    if (error || !is_within(project_root_, candidate)) return false;
    if (!std::filesystem::is_regular_file(candidate, error) || error) return false;

    const auto size = std::filesystem::file_size(candidate, error);
    if (error || size != static_cast<std::uintmax_t>(artifact.size_bytes)) return false;

    try {
        return sha256_file_hex(candidate) == artifact.sha256;
    } catch (const std::exception&) {
        return false;
    }
}

}  // namespace biocore::infrastructure
