#pragma once

#include "biocore/domain/workflow_checkpoint.hpp"

namespace biocore::application {

class IWorkflowCheckpointArtifactVerifier {
public:
    virtual ~IWorkflowCheckpointArtifactVerifier() = default;

    [[nodiscard]] virtual bool verify(
        const domain::WorkflowCheckpointArtifact& artifact
    ) const = 0;
};

}  // namespace biocore::application
