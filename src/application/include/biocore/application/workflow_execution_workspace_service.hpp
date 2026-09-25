#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_workflow_state_store.hpp"
#include "biocore/application/workflow_resume_planner.hpp"
#include "biocore/domain/workflow.hpp"
#include "biocore/domain/workflow_branch_decision.hpp"
#include "biocore/domain/workflow_checkpoint.hpp"

namespace biocore::application {

class IWorkflowCheckpointArtifactVerifier;
class WorkflowStateService;

struct WorkflowExecutionWorkspaceSummary final {
    std::string workflow_id;
    std::string name;
    std::int64_t revision{0};
    std::string updated_at_utc;
    std::size_t node_count{0U};
    std::size_t pending_count{0U};
    std::size_t running_count{0U};
    std::size_t completed_count{0U};
    std::size_t failed_count{0U};
    std::size_t interrupted_count{0U};
    std::size_t skipped_count{0U};
    std::size_t blocked_count{0U};
};

struct WorkflowExecutionNodeView final {
    std::string node_id;
    std::string label;
    std::string module_id;
    std::string plugin_version;

    domain::WorkflowCheckpointNodeState checkpoint_state{
        domain::WorkflowCheckpointNodeState::pending
    };
    std::uint32_t attempt_number{0U};
    std::uint32_t max_attempts{1U};
    std::vector<domain::WorkflowCheckpointArtifact> outputs;
    std::optional<domain::WorkflowCheckpointFailure> failure;

    std::optional<domain::WorkflowBranchDecisionState> branch_state;
    std::optional<domain::WorkflowBranchDecisionReason> branch_reason;
    std::optional<bool> condition_result;

    WorkflowResumeActionKind resume_action{WorkflowResumeActionKind::execute};
    WorkflowResumeReason resume_reason{WorkflowResumeReason::pending_execution};
    std::optional<std::uint32_t> next_attempt_number;
};

struct WorkflowExecutionWorkspaceSnapshot final {
    std::string workflow_id;
    std::string name;
    std::string description;
    std::int64_t revision{0};
    std::string updated_at_utc;
    std::vector<WorkflowExecutionNodeView> nodes;
};

class WorkflowExecutionWorkspaceService final {
public:
    WorkflowExecutionWorkspaceService(
        WorkflowStateService& states,
        const IWorkflowCheckpointArtifactVerifier& verifier
    ) noexcept;

    [[nodiscard]] WorkflowExecutionWorkspaceSnapshot create(
        const domain::Workflow& workflow,
        const WorkflowRetryPolicy& retry_policy = {}
    );

    [[nodiscard]] std::vector<WorkflowExecutionWorkspaceSummary> list();

    [[nodiscard]] std::optional<WorkflowExecutionWorkspaceSnapshot> find(
        std::string_view workflow_id
    );

private:
    [[nodiscard]] WorkflowExecutionWorkspaceSnapshot build_snapshot(
        const PersistedWorkflowState& state
    ) const;

    WorkflowStateService& states_;
    const IWorkflowCheckpointArtifactVerifier& verifier_;
};

}  // namespace biocore::application
