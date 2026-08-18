#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

#include "biocore/application/generated_output_artifact.hpp"
#include "biocore/infrastructure/filesystem_artifact_content_access.hpp"

namespace {
namespace fs = std::filesystem;
using namespace biocore;

constexpr std::string_view alpha_sha =
    "b6a98d9ce9a2d9149288fa3df42d377c3e42737afdcdaf714e33c0a100b51060";

class Temp final {
public:
    Temp() {
        root = fs::temp_directory_path() /
               ("biocore-artifact-content-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root / "outputs");
    }
    ~Temp() { std::error_code error; fs::remove_all(root, error); }
    fs::path root;
};

[[nodiscard]] application::GeneratedOutputArtifact artifact(
    const fs::path& root,
    std::optional<std::string> checksum = std::string{alpha_sha},
    std::int64_t size = 6,
    std::string relative = "outputs/job-1--copy--result.out"
) {
    const auto absolute = fs::canonical(root) / fs::path{relative};
    domain::ManagedFile file{
        "artifact-1", "job-1--copy--result.out", domain::StorageMode::generated_output,
        std::nullopt, absolute.generic_string(), relative, "txt", size, std::nullopt,
        checksum.has_value() ? std::optional<std::string>{"sha256"} : std::nullopt,
        std::move(checksum), "c", "u",
    };
    application::GeneratedOutputProvenance provenance{
        .job_id = "job-1", .step_id = "copy", .output_port = "result",
        .plugin_id = "org.biocore.demo", .plugin_version = "0.1.0",
        .module_id = "org.biocore.demo.copy", .file_type = "txt",
        .relative_project_path = std::move(relative), .step_progress = 1.0,
        .registered_at_utc = "u",
    };
    return {std::move(file), std::move(provenance)};
}

[[nodiscard]] bool verified_then_tamper_contract() {
    Temp temp;
    const auto output = temp.root / "outputs" / "job-1--copy--result.out";
    { std::ofstream stream{output, std::ios::binary}; stream << "alpha\n"; }
    infrastructure::FilesystemArtifactContentAccess access{fs::canonical(temp.root)};
    const auto good = access.verify_for_download(artifact(temp.root));
    if (good.status != application::ArtifactContentStatus::verified ||
        good.computed_sha256 != std::optional<std::string>{std::string{alpha_sha}} ||
        !good.content_path.has_value()) {
        return false;
    }

    { std::ofstream stream{output, std::ios::binary | std::ios::trunc}; stream << "bravo\n"; }
    const auto tampered = access.verify_for_download(artifact(temp.root));
    if (tampered.status != application::ArtifactContentStatus::checksum_mismatch) return false;

    { std::ofstream stream{output, std::ios::binary | std::ios::app}; stream << "x"; }
    const auto wrong_size = access.verify_for_download(artifact(temp.root));
    return wrong_size.status == application::ArtifactContentStatus::size_mismatch;
}

[[nodiscard]] bool unsafe_and_legacy_contract() {
    Temp temp;
    const auto output = temp.root / "outputs" / "job-1--copy--result.out";
    { std::ofstream stream{output, std::ios::binary}; stream << "alpha\n"; }
    infrastructure::FilesystemArtifactContentAccess access{fs::canonical(temp.root)};

    if (access.verify_for_download(artifact(temp.root, std::nullopt)).status !=
        application::ArtifactContentStatus::checksum_unavailable) {
        return false;
    }

#ifndef _WIN32
    const auto link = temp.root / "outputs" / "job-1--copy--link.out";
    std::error_code error;
    fs::create_symlink(output, link, error);
    if (!error) {
        auto linked = artifact(
            temp.root,
            std::string{alpha_sha},
            6,
            "outputs/job-1--copy--link.out"
        );
        if (access.verify_for_download(linked).status != application::ArtifactContentStatus::unsafe_path) {
            return false;
        }
    }
#endif
    return true;
}

}  // namespace

int main() {
    if (!verified_then_tamper_contract() || !unsafe_and_legacy_contract()) {
        std::cerr << "Filesystem artifact content access tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
