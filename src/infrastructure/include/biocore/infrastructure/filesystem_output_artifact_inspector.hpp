#pragma once

#include <filesystem>

#include "biocore/application/i_output_artifact_inspector.hpp"

namespace biocore::infrastructure {

class FilesystemOutputArtifactInspector final : public application::IOutputArtifactInspector {
public:
    explicit FilesystemOutputArtifactInspector(std::filesystem::path project_root);

    [[nodiscard]] application::InspectedOutputArtifact inspect_existing_output(
        std::string_view relative_project_path
    ) override;

private:
    std::filesystem::path project_root_;
    std::filesystem::path outputs_directory_;
};

}  // namespace biocore::infrastructure
