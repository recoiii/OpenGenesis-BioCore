#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_workflow_state_store.hpp"
#include "biocore/application/workflow_resume_planner.hpp"

namespace biocore::application {

class IUtcClock;

enum class WorkflowStateRecoveryIssueStage {
    load_state,
    reconcile_running_checkpoint,
    persist_reconciled_state,
    plan_resume,
};

[[nodiscard]] std::string_view to_string(
    WorkflowStateRecoveryIssueStage stage
) noexcept;

struct WorkflowStateRecoveryIssue final {
    WorkflowStateRecoveryIssueStage stage{
        WorkflowStateRecoveryIssueStage::load_state
    };
    std::optional<std::string> workflow_id;
    std::string message;
};

struct RecoveredWorkflowState final {
    domain::WorkflowId workflow_id;
    std::int64_t revision{0};
    bool reconciled_running_checkpoint{false};
    WorkflowResumePlan resume_plan;
};

struct WorkflowStateRecoveryResult final {
    std::vector<RecoveredWorkflowState> recovered;
    std::vector<WorkflowStateRecoveryIssue> issues;
};

class WorkflowStateRecoveryService final {
public:
    WorkflowStateRecoveryService(
        IWorkflowStateStore& store,
        const IWorkflowCheckpointArtifactVerifier& verifier,
        IUtcClock& clock
    ) noexcept;

    [[nodiscard]] WorkflowStateRecoveryResult recover();

private:
    IWorkflowStateStore& store_;
    const IWorkflowCheckpointArtifactVerifier& verifier_;
    IUtcClock& clock_;
};

}  // namespace biocore::application
