#pragma once

#include <filesystem>

#include "biocore/application/i_result_artifact_reader.hpp"
#include "biocore/infrastructure/filesystem_artifact_content_access.hpp"

namespace biocore::infrastructure {

class FilesystemResultArtifactReader final : public application::IResultArtifactReader {
public:
    explicit FilesystemResultArtifactReader(std::filesystem::path project_root);

    [[nodiscard]] application::ResultArtifactText read_verified_text(
        const application::GeneratedOutputArtifact& artifact,
        std::size_t maximum_bytes
    ) override;

private:
    FilesystemArtifactContentAccess verifier_;
};

}  // namespace biocore::infrastructure
