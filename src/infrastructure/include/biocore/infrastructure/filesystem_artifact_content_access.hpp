#pragma once

#include <filesystem>

#include "biocore/application/i_artifact_content_access.hpp"

namespace biocore::infrastructure {

class FilesystemArtifactContentAccess final : public application::IArtifactContentAccess {
public:
    explicit FilesystemArtifactContentAccess(std::filesystem::path project_root);

    [[nodiscard]] application::ArtifactContentVerification verify_for_download(
        const application::GeneratedOutputArtifact& artifact
    ) override;

private:
    std::filesystem::path project_root_;
    std::filesystem::path outputs_directory_;
};

}  // namespace biocore::infrastructure
