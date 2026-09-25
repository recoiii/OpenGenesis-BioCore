#pragma once

#include <filesystem>

#include "biocore/application/i_workflow_checkpoint_artifact_verifier.hpp"

namespace biocore::infrastructure {

class FilesystemWorkflowCheckpointArtifactVerifier final
    : public application::IWorkflowCheckpointArtifactVerifier {
public:
    explicit FilesystemWorkflowCheckpointArtifactVerifier(
        std::filesystem::path project_root
    );

    [[nodiscard]] bool verify(
        const domain::WorkflowCheckpointArtifact& artifact
    ) const override;

private:
    std::filesystem::path project_root_;
};

}  // namespace biocore::infrastructure
