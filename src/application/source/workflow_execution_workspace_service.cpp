#include "biocore/application/workflow_execution_workspace_service.hpp"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

#include "biocore/application/i_workflow_checkpoint_artifact_verifier.hpp"
#include "biocore/application/workflow_state_service.hpp"

namespace biocore::application {
namespace {

[[nodiscard]] WorkflowExecutionWorkspaceSummary summarize(
    const PersistedWorkflowState& state
) {
    WorkflowExecutionWorkspaceSummary summary{
        .workflow_id = std::string{state.workflow.id().value()},
        .name = std::string{state.workflow.name()},
        .revision = state.revision,
        .updated_at_utc = state.updated_at_utc,
        .node_count = state.checkpoint.nodes().size(),
    };

    for (const domain::WorkflowNodeCheckpoint& node : state.checkpoint.nodes()) {
        switch (node.state) {
            case domain::WorkflowCheckpointNodeState::pending:
                ++summary.pending_count;
                break;
            case domain::WorkflowCheckpointNodeState::running:
                ++summary.running_count;
                break;
            case domain::WorkflowCheckpointNodeState::completed:
                ++summary.completed_count;
                break;
            case domain::WorkflowCheckpointNodeState::failed:
                ++summary.failed_count;
                break;
            case domain::WorkflowCheckpointNodeState::interrupted:
                ++summary.interrupted_count;
                break;
            case domain::WorkflowCheckpointNodeState::skipped:
                ++summary.skipped_count;
                break;
            case domain::WorkflowCheckpointNodeState::blocked:
                ++summary.blocked_count;
                break;
        }
    }
    return summary;
}

}  // namespace

WorkflowExecutionWorkspaceService::WorkflowExecutionWorkspaceService(
    WorkflowStateService& states,
    const IWorkflowCheckpointArtifactVerifier& verifier
) noexcept
    : states_{states}, verifier_{verifier} {}

WorkflowExecutionWorkspaceSnapshot WorkflowExecutionWorkspaceService::create(
    const domain::Workflow& workflow,
    const WorkflowRetryPolicy& retry_policy
) {
    const PersistedWorkflowState state =
        states_.create(workflow, retry_policy, std::nullopt);
    return build_snapshot(state);
}

std::vector<WorkflowExecutionWorkspaceSummary>
WorkflowExecutionWorkspaceService::list() {
    std::vector<WorkflowExecutionWorkspaceSummary> result;
    for (const PersistedWorkflowState& state : states_.list()) {
        result.push_back(summarize(state));
    }
    std::ranges::sort(
        result,
        [](const auto& left, const auto& right) {
            return left.workflow_id < right.workflow_id;
        }
    );
    return result;
}

std::optional<WorkflowExecutionWorkspaceSnapshot>
WorkflowExecutionWorkspaceService::find(
    const std::string_view workflow_id
) {
    const auto state = states_.find(workflow_id);
    if (!state.has_value()) return std::nullopt;
    return build_snapshot(*state);
}

WorkflowExecutionWorkspaceSnapshot
WorkflowExecutionWorkspaceService::build_snapshot(
    const PersistedWorkflowState& state
) const {
    const WorkflowResumePlan resume = plan_workflow_resume(
        state.workflow, state.checkpoint, verifier_
    );

    std::map<
        std::string,
        const domain::WorkflowNodeCheckpoint*,
        std::less<>
    > checkpoints;
    for (const auto& checkpoint : state.checkpoint.nodes()) {
        checkpoints.emplace(
            std::string{checkpoint.node_id.value()},
            &checkpoint
        );
    }

    std::map<
        std::string,
        const domain::WorkflowBranchDecision*,
        std::less<>
    > branches;
    if (state.branch_decisions.has_value()) {
        for (const auto& decision : state.branch_decisions->decisions()) {
            branches.emplace(
                std::string{decision.node_id.value()},
                &decision
            );
        }
    }

    std::map<
        std::string,
        const WorkflowNodeResumeAction*,
        std::less<>
    > actions;
    for (const auto& action : resume.actions()) {
        actions.emplace(
            std::string{action.node_id.value()},
            &action
        );
    }

    std::vector<WorkflowExecutionNodeView> nodes;
    nodes.reserve(state.workflow.nodes().size());

    for (const domain::WorkflowNode& node : state.workflow.nodes()) {
        const std::string node_id{node.id().value()};
        const auto checkpoint = checkpoints.find(node_id);
        const auto action = actions.find(node_id);
        if (checkpoint == checkpoints.end() ||
            action == actions.end()) {
            throw std::logic_error(
                "Persisted workflow state is missing execution workspace node state"
            );
        }

        WorkflowExecutionNodeView view{
            .node_id = node_id,
            .label = std::string{node.label()},
            .module_id = std::string{node.module_id()},
            .plugin_version = std::string{node.plugin_version()},
            .checkpoint_state = checkpoint->second->state,
            .attempt_number = checkpoint->second->attempt_number,
            .max_attempts = checkpoint->second->max_attempts,
            .outputs = checkpoint->second->outputs,
            .failure = checkpoint->second->failure,
            .branch_state = std::nullopt,
            .branch_reason = std::nullopt,
            .condition_result = std::nullopt,
            .resume_action = action->second->action,
            .resume_reason = action->second->reason,
            .next_attempt_number = action->second->next_attempt_number,
        };

        const auto branch = branches.find(node_id);
        if (branch != branches.end()) {
            view.branch_state = branch->second->state;
            view.branch_reason = branch->second->reason;
            view.condition_result = branch->second->condition_result;
        }
        nodes.push_back(std::move(view));
    }

    return WorkflowExecutionWorkspaceSnapshot{
        .workflow_id = std::string{state.workflow.id().value()},
        .name = std::string{state.workflow.name()},
        .description = std::string{state.workflow.description()},
        .revision = state.revision,
        .updated_at_utc = state.updated_at_utc,
        .nodes = std::move(nodes),
    };
}

}  // namespace biocore::application
