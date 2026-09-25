#include "biocore/application/workflow_state_recovery_service.hpp"

#include <exception>
#include <limits>
#include <string>
#include <utility>

#include "biocore/application/i_utc_clock.hpp"

namespace biocore::application {
namespace {

[[nodiscard]] WorkflowStateRecoveryIssue issue(
    const WorkflowStateRecoveryIssueStage stage,
    std::optional<std::string> workflow_id,
    std::string message
) {
    return WorkflowStateRecoveryIssue{
        .stage = stage,
        .workflow_id = std::move(workflow_id),
        .message = std::move(message),
    };
}

[[nodiscard]] std::pair<domain::WorkflowCheckpointManifest, bool>
reconcile_running(
    const domain::WorkflowCheckpointManifest& manifest
) {
    std::vector<domain::WorkflowNodeCheckpoint> nodes = manifest.nodes();
    bool changed = false;
    for (domain::WorkflowNodeCheckpoint& node : nodes) {
        if (node.state != domain::WorkflowCheckpointNodeState::running) {
            continue;
        }
        node.state = domain::WorkflowCheckpointNodeState::interrupted;
        node.failure = domain::WorkflowCheckpointFailure{
            "Workflow node was running when the project was reopened.",
            std::nullopt,
        };
        changed = true;
    }
    return {
        domain::WorkflowCheckpointManifest{
            manifest.schema_version(),
            manifest.workflow_id(),
            std::move(nodes),
        },
        changed,
    };
}

}  // namespace

std::string_view to_string(
    const WorkflowStateRecoveryIssueStage stage
) noexcept {
    switch (stage) {
        case WorkflowStateRecoveryIssueStage::load_state:
            return "load_state";
        case WorkflowStateRecoveryIssueStage::reconcile_running_checkpoint:
            return "reconcile_running_checkpoint";
        case WorkflowStateRecoveryIssueStage::persist_reconciled_state:
            return "persist_reconciled_state";
        case WorkflowStateRecoveryIssueStage::plan_resume:
            return "plan_resume";
    }
    return "unknown";
}

WorkflowStateRecoveryService::WorkflowStateRecoveryService(
    IWorkflowStateStore& store,
    const IWorkflowCheckpointArtifactVerifier& verifier,
    IUtcClock& clock
) noexcept
    : store_{store}, verifier_{verifier}, clock_{clock} {}

WorkflowStateRecoveryResult WorkflowStateRecoveryService::recover() {
    WorkflowStateRecoveryResult result;
    std::vector<PersistedWorkflowState> states;
    try {
        states = store_.list();
    } catch (const std::exception& error) {
        result.issues.push_back(issue(
            WorkflowStateRecoveryIssueStage::load_state,
            std::nullopt,
            error.what()
        ));
        return result;
    } catch (...) {
        result.issues.push_back(issue(
            WorkflowStateRecoveryIssueStage::load_state,
            std::nullopt,
            "Unknown workflow state loading failure"
        ));
        return result;
    }

    for (PersistedWorkflowState state : states) {
        const std::string workflow_id{state.workflow.id().value()};
        bool reconciled = false;

        try {
            auto [checkpoint, changed] = reconcile_running(state.checkpoint);
            if (changed) {
                if (state.revision == std::numeric_limits<std::int64_t>::max()) {
                    throw std::overflow_error(
                        "Workflow state revision cannot advance during startup recovery"
                    );
                }
                const std::int64_t expected_revision = state.revision;
                state.checkpoint = std::move(checkpoint);
                state.revision += 1;
                state.updated_at_utc = clock_.now_utc_iso8601();
                if (!store_.update(state, expected_revision)) {
                    result.issues.push_back(issue(
                        WorkflowStateRecoveryIssueStage::persist_reconciled_state,
                        workflow_id,
                        "Workflow state changed during startup reconciliation"
                    ));
                    continue;
                }
                reconciled = true;
            }
        } catch (const std::exception& error) {
            result.issues.push_back(issue(
                WorkflowStateRecoveryIssueStage::reconcile_running_checkpoint,
                workflow_id,
                error.what()
            ));
            continue;
        } catch (...) {
            result.issues.push_back(issue(
                WorkflowStateRecoveryIssueStage::reconcile_running_checkpoint,
                workflow_id,
                "Unknown workflow checkpoint reconciliation failure"
            ));
            continue;
        }

        try {
            auto plan = plan_workflow_resume(
                state.workflow, state.checkpoint, verifier_
            );
            result.recovered.push_back(RecoveredWorkflowState{
                .workflow_id = state.workflow.id(),
                .revision = state.revision,
                .reconciled_running_checkpoint = reconciled,
                .resume_plan = std::move(plan),
            });
        } catch (const std::exception& error) {
            result.issues.push_back(issue(
                WorkflowStateRecoveryIssueStage::plan_resume,
                workflow_id,
                error.what()
            ));
        } catch (...) {
            result.issues.push_back(issue(
                WorkflowStateRecoveryIssueStage::plan_resume,
                workflow_id,
                "Unknown workflow resume planning failure"
            ));
        }
    }

    return result;
}

}  // namespace biocore::application
