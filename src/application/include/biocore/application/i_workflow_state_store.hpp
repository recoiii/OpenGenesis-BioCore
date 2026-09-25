#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/domain/workflow.hpp"
#include "biocore/domain/workflow_branch_decision.hpp"
#include "biocore/domain/workflow_checkpoint.hpp"

namespace biocore::application {

struct PersistedWorkflowState final {
    domain::Workflow workflow;
    domain::WorkflowCheckpointManifest checkpoint;
    std::optional<domain::WorkflowBranchDecisionSnapshot> branch_decisions;
    std::int64_t revision{0};
    std::string updated_at_utc;
};

class IWorkflowStateStore {
public:
    virtual ~IWorkflowStateStore() = default;

    virtual bool create(const PersistedWorkflowState& state) = 0;
    virtual bool update(
        const PersistedWorkflowState& state,
        std::int64_t expected_revision
    ) = 0;

    [[nodiscard]] virtual std::optional<PersistedWorkflowState> find_by_workflow_id(
        std::string_view workflow_id
    ) = 0;

    [[nodiscard]] virtual std::vector<PersistedWorkflowState> list() = 0;
};

}  // namespace biocore::application
