#include "biocore/application/workflow_resume_planner.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <utility>

#include "biocore/domain/workflow_dag.hpp"

namespace biocore::application {
namespace {

using NodeLookup = std::map<std::string, const domain::WorkflowNode*, std::less<>>;
using CheckpointLookup =
    std::map<std::string, const domain::WorkflowNodeCheckpoint*, std::less<>>;
using ActionLookup =
    std::map<std::string, const WorkflowNodeResumeAction*, std::less<>>;

[[noreturn]] void fail(
    const WorkflowResumeErrorCode code,
    const std::string& message
) {
    throw WorkflowResumeError{code, "Workflow resume planning failed: " + message};
}

[[nodiscard]] const domain::WorkflowInputDeclaration* find_input(
    const domain::WorkflowNode& node,
    const std::string_view port
) noexcept {
    const auto iterator = std::ranges::find_if(
        node.inputs(), [port](const auto& input) { return input.name() == port; }
    );
    return iterator == node.inputs().end() ? nullptr : &*iterator;
}

[[nodiscard]] bool action_recomputes(const WorkflowResumeActionKind action) noexcept {
    return action == WorkflowResumeActionKind::execute ||
           action == WorkflowResumeActionKind::retry ||
           action == WorkflowResumeActionKind::reevaluate_condition;
}

[[nodiscard]] bool action_unavailable(const WorkflowResumeActionKind action) noexcept {
    return action == WorkflowResumeActionKind::preserve_skipped ||
           action == WorkflowResumeActionKind::preserve_blocked ||
           action == WorkflowResumeActionKind::exhausted;
}

[[nodiscard]] bool completed_outputs_are_valid(
    const domain::WorkflowNode& node,
    const domain::WorkflowNodeCheckpoint& checkpoint,
    const IWorkflowCheckpointArtifactVerifier& verifier
) {
    if (checkpoint.outputs.size() != node.outputs().size()) {
        return false;
    }

    std::map<std::string, const domain::WorkflowCheckpointArtifact*, std::less<>>
        checkpoint_outputs;
    for (const auto& output : checkpoint.outputs) {
        checkpoint_outputs.emplace(output.output_port, &output);
    }

    for (const domain::WorkflowOutputDeclaration& declared : node.outputs()) {
        const auto iterator =
            checkpoint_outputs.find(std::string{declared.name()});
        if (iterator == checkpoint_outputs.end() ||
            !verifier.verify(*iterator->second)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] WorkflowNodeResumeAction retry_or_exhausted(
    const domain::WorkflowNodeCheckpoint& checkpoint,
    const WorkflowResumeReason retry_reason
) {
    if (checkpoint.attempt_number < checkpoint.max_attempts) {
        return WorkflowNodeResumeAction{
            .node_id = checkpoint.node_id,
            .action = WorkflowResumeActionKind::retry,
            .reason = retry_reason,
            .next_attempt_number = checkpoint.attempt_number + 1U,
            .previous_failure = checkpoint.failure,
        };
    }
    return WorkflowNodeResumeAction{
        .node_id = checkpoint.node_id,
        .action = WorkflowResumeActionKind::exhausted,
        .reason = WorkflowResumeReason::attempt_limit_reached,
        .next_attempt_number = std::nullopt,
        .previous_failure = checkpoint.failure,
    };
}

}  // namespace

std::string_view to_string(const WorkflowResumeActionKind kind) noexcept {
    switch (kind) {
        case WorkflowResumeActionKind::reuse_completed: return "reuse_completed";
        case WorkflowResumeActionKind::execute: return "execute";
        case WorkflowResumeActionKind::retry: return "retry";
        case WorkflowResumeActionKind::reevaluate_condition:
            return "reevaluate_condition";
        case WorkflowResumeActionKind::preserve_skipped: return "preserve_skipped";
        case WorkflowResumeActionKind::preserve_blocked: return "preserve_blocked";
        case WorkflowResumeActionKind::exhausted: return "exhausted";
    }
    return "unknown";
}

std::string_view to_string(const WorkflowResumeReason reason) noexcept {
    switch (reason) {
        case WorkflowResumeReason::completed_verified: return "completed_verified";
        case WorkflowResumeReason::pending_execution: return "pending_execution";
        case WorkflowResumeReason::failed_retry: return "failed_retry";
        case WorkflowResumeReason::interrupted_retry: return "interrupted_retry";
        case WorkflowResumeReason::incomplete_retry: return "incomplete_retry";
        case WorkflowResumeReason::artifact_integrity_failed:
            return "artifact_integrity_failed";
        case WorkflowResumeReason::upstream_recomputed: return "upstream_recomputed";
        case WorkflowResumeReason::checkpoint_skipped: return "checkpoint_skipped";
        case WorkflowResumeReason::checkpoint_blocked: return "checkpoint_blocked";
        case WorkflowResumeReason::dependency_unavailable:
            return "dependency_unavailable";
        case WorkflowResumeReason::attempt_limit_reached:
            return "attempt_limit_reached";
    }
    return "unknown";
}

WorkflowResumePlan::WorkflowResumePlan(
    domain::WorkflowId workflow_id,
    std::vector<WorkflowNodeResumeAction> actions
)
    : workflow_id_{std::move(workflow_id)}, actions_{std::move(actions)} {}

const domain::WorkflowId& WorkflowResumePlan::workflow_id() const noexcept {
    return workflow_id_;
}

const std::vector<WorkflowNodeResumeAction>& WorkflowResumePlan::actions() const noexcept {
    return actions_;
}

std::string_view to_string(const WorkflowResumeErrorCode code) noexcept {
    switch (code) {
        case WorkflowResumeErrorCode::invalid_retry_policy: return "invalid_retry_policy";
        case WorkflowResumeErrorCode::duplicate_retry_override:
            return "duplicate_retry_override";
        case WorkflowResumeErrorCode::unknown_retry_override_node:
            return "unknown_retry_override_node";
        case WorkflowResumeErrorCode::workflow_id_mismatch: return "workflow_id_mismatch";
        case WorkflowResumeErrorCode::missing_checkpoint_node:
            return "missing_checkpoint_node";
        case WorkflowResumeErrorCode::unknown_checkpoint_node:
            return "unknown_checkpoint_node";
    }
    return "unknown";
}

WorkflowResumeError::WorkflowResumeError(
    const WorkflowResumeErrorCode code,
    std::string message
)
    : std::invalid_argument{std::move(message)}, code_{code} {}

WorkflowResumeErrorCode WorkflowResumeError::code() const noexcept { return code_; }

domain::WorkflowCheckpointManifest initialize_workflow_checkpoint_manifest(
    const domain::Workflow& workflow,
    const WorkflowRetryPolicy& policy
) {
    const domain::WorkflowDagPlan dag = domain::plan_workflow_dag(workflow);
    if (policy.default_max_attempts == 0U ||
        policy.default_max_attempts > domain::WorkflowCheckpointManifest::maximum_attempts) {
        fail(
            WorkflowResumeErrorCode::invalid_retry_policy,
            "default retry limit is outside the supported range"
        );
    }

    std::map<std::string, std::uint32_t, std::less<>> limits;
    for (const domain::WorkflowPlanNode& planned : dag.ordered_nodes()) {
        limits.emplace(
            std::string{planned.id.value()},
            policy.default_max_attempts
        );
    }

    std::set<std::string, std::less<>> overridden;
    for (const WorkflowRetryLimitOverride& override_value : policy.overrides) {
        const std::string node_id{override_value.node_id.value()};
        if (!limits.contains(node_id)) {
            fail(
                WorkflowResumeErrorCode::unknown_retry_override_node,
                "retry override references unknown node '" + node_id + "'"
            );
        }
        if (!overridden.emplace(node_id).second) {
            fail(
                WorkflowResumeErrorCode::duplicate_retry_override,
                "retry override for node '" + node_id + "' is duplicated"
            );
        }
        if (override_value.max_attempts == 0U ||
            override_value.max_attempts >
                domain::WorkflowCheckpointManifest::maximum_attempts) {
            fail(
                WorkflowResumeErrorCode::invalid_retry_policy,
                "retry override for node '" + node_id + "' is invalid"
            );
        }
        limits.at(node_id) = override_value.max_attempts;
    }

    std::vector<domain::WorkflowNodeCheckpoint> checkpoints;
    checkpoints.reserve(dag.ordered_nodes().size());
    for (const domain::WorkflowPlanNode& planned : dag.ordered_nodes()) {
        checkpoints.push_back(domain::WorkflowNodeCheckpoint{
            .node_id = planned.id,
            .state = domain::WorkflowCheckpointNodeState::pending,
            .attempt_number = 0U,
            .max_attempts = limits.at(std::string{planned.id.value()}),
            .outputs = {},
            .failure = std::nullopt,
        });
    }

    return domain::WorkflowCheckpointManifest{
        domain::WorkflowCheckpointManifest::current_schema_version,
        workflow.id(),
        std::move(checkpoints),
    };
}

WorkflowResumePlan plan_workflow_resume(
    const domain::Workflow& workflow,
    const domain::WorkflowCheckpointManifest& manifest,
    const IWorkflowCheckpointArtifactVerifier& verifier
) {
    const domain::WorkflowDagPlan dag = domain::plan_workflow_dag(workflow);
    if (manifest.workflow_id() != workflow.id()) {
        fail(
            WorkflowResumeErrorCode::workflow_id_mismatch,
            "checkpoint manifest belongs to a different workflow"
        );
    }

    NodeLookup nodes;
    for (const domain::WorkflowNode& node : workflow.nodes()) {
        nodes.emplace(std::string{node.id().value()}, &node);
    }

    CheckpointLookup checkpoints;
    for (const domain::WorkflowNodeCheckpoint& checkpoint : manifest.nodes()) {
        const std::string node_id{checkpoint.node_id.value()};
        if (!nodes.contains(node_id)) {
            fail(
                WorkflowResumeErrorCode::unknown_checkpoint_node,
                "checkpoint references unknown node '" + node_id + "'"
            );
        }
        checkpoints.emplace(node_id, &checkpoint);
    }
    for (const auto& [node_id, node] : nodes) {
        static_cast<void>(node);
        if (!checkpoints.contains(node_id)) {
            fail(
                WorkflowResumeErrorCode::missing_checkpoint_node,
                "checkpoint is missing node '" + node_id + "'"
            );
        }
    }

    std::map<std::string, std::vector<const domain::WorkflowEdge*>, std::less<>>
        incoming_edges;
    for (const domain::WorkflowEdge& edge : workflow.edges()) {
        incoming_edges[std::string{edge.target_node_id().value()}].push_back(&edge);
    }

    std::vector<WorkflowNodeResumeAction> actions;
    actions.reserve(dag.ordered_nodes().size());
    ActionLookup prior_actions;

    for (const domain::WorkflowPlanNode& planned : dag.ordered_nodes()) {
        const std::string node_id{planned.id.value()};
        const domain::WorkflowNode& node = *nodes.at(node_id);
        const domain::WorkflowNodeCheckpoint& checkpoint = *checkpoints.at(node_id);

        bool required_dependency_unavailable = false;
        bool upstream_recomputed = false;
        for (const domain::WorkflowEdge* edge : incoming_edges[node_id]) {
            const std::string source_id{edge->source_node_id().value()};
            const auto source_action = prior_actions.find(source_id);
            if (source_action == prior_actions.end()) continue;

            if (action_recomputes(source_action->second->action)) {
                upstream_recomputed = true;
            }
            if (action_unavailable(source_action->second->action)) {
                const auto* input = find_input(node, edge->target_input());
                if (input != nullptr && input->required()) {
                    required_dependency_unavailable = true;
                    break;
                }
            }
        }

        WorkflowNodeResumeAction action{
            .node_id = checkpoint.node_id,
            .action = WorkflowResumeActionKind::execute,
            .reason = WorkflowResumeReason::pending_execution,
            .next_attempt_number = std::nullopt,
            .previous_failure = checkpoint.failure,
        };

        if (required_dependency_unavailable) {
            action.action = WorkflowResumeActionKind::preserve_blocked;
            action.reason = WorkflowResumeReason::dependency_unavailable;
            action.next_attempt_number = std::nullopt;
        } else if (
            upstream_recomputed &&
            (checkpoint.state == domain::WorkflowCheckpointNodeState::skipped ||
             checkpoint.state == domain::WorkflowCheckpointNodeState::blocked)
        ) {
            action.action = WorkflowResumeActionKind::reevaluate_condition;
            action.reason = WorkflowResumeReason::upstream_recomputed;
            action.next_attempt_number = std::nullopt;
        } else {
            switch (checkpoint.state) {
                case domain::WorkflowCheckpointNodeState::pending:
                    action.action = WorkflowResumeActionKind::execute;
                    action.reason = WorkflowResumeReason::pending_execution;
                    action.next_attempt_number = 1U;
                    break;
                case domain::WorkflowCheckpointNodeState::running:
                    action = retry_or_exhausted(
                        checkpoint, WorkflowResumeReason::incomplete_retry
                    );
                    break;
                case domain::WorkflowCheckpointNodeState::failed:
                    action = retry_or_exhausted(
                        checkpoint, WorkflowResumeReason::failed_retry
                    );
                    break;
                case domain::WorkflowCheckpointNodeState::interrupted:
                    action = retry_or_exhausted(
                        checkpoint, WorkflowResumeReason::interrupted_retry
                    );
                    break;
                case domain::WorkflowCheckpointNodeState::skipped:
                    action.action = WorkflowResumeActionKind::preserve_skipped;
                    action.reason = WorkflowResumeReason::checkpoint_skipped;
                    action.next_attempt_number = std::nullopt;
                    break;
                case domain::WorkflowCheckpointNodeState::blocked:
                    action.action = WorkflowResumeActionKind::preserve_blocked;
                    action.reason = WorkflowResumeReason::checkpoint_blocked;
                    action.next_attempt_number = std::nullopt;
                    break;
                case domain::WorkflowCheckpointNodeState::completed:
                    if (!upstream_recomputed &&
                        completed_outputs_are_valid(node, checkpoint, verifier)) {
                        action.action = WorkflowResumeActionKind::reuse_completed;
                        action.reason = WorkflowResumeReason::completed_verified;
                        action.next_attempt_number = std::nullopt;
                    } else {
                        action = retry_or_exhausted(
                            checkpoint,
                            upstream_recomputed
                                ? WorkflowResumeReason::upstream_recomputed
                                : WorkflowResumeReason::artifact_integrity_failed
                        );
                    }
                    break;
            }
        }

        actions.push_back(std::move(action));
        prior_actions.emplace(node_id, &actions.back());
    }

    return WorkflowResumePlan{workflow.id(), std::move(actions)};
}

}  // namespace biocore::application
