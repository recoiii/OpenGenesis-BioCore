#include "biocore/application/workflow_state_service.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

#include "biocore/application/i_utc_clock.hpp"

namespace biocore::application {
namespace {

void require_identity(
    const domain::Workflow& workflow,
    const domain::WorkflowCheckpointManifest& checkpoint,
    const std::optional<domain::WorkflowBranchDecisionSnapshot>& branch_decisions
) {
    if (checkpoint.workflow_id() != workflow.id() ||
        (branch_decisions.has_value() &&
         branch_decisions->workflow_id() != workflow.id())) {
        throw WorkflowStateServiceError{
            WorkflowStateServiceErrorCode::workflow_identity_mismatch,
            "Workflow checkpoint or branch decisions belong to a different workflow"
        };
    }
}

}  // namespace

std::string_view to_string(const WorkflowStateServiceErrorCode code) noexcept {
    switch (code) {
        case WorkflowStateServiceErrorCode::workflow_state_exists:
            return "workflow_state_exists";
        case WorkflowStateServiceErrorCode::workflow_state_not_found:
            return "workflow_state_not_found";
        case WorkflowStateServiceErrorCode::workflow_identity_mismatch:
            return "workflow_identity_mismatch";
        case WorkflowStateServiceErrorCode::concurrent_update:
            return "concurrent_update";
        case WorkflowStateServiceErrorCode::revision_overflow:
            return "revision_overflow";
    }
    return "unknown";
}

WorkflowStateServiceError::WorkflowStateServiceError(
    const WorkflowStateServiceErrorCode code,
    std::string message
)
    : std::runtime_error{std::move(message)}, code_{code} {}

WorkflowStateServiceErrorCode WorkflowStateServiceError::code() const noexcept {
    return code_;
}

WorkflowStateService::WorkflowStateService(
    IWorkflowStateStore& store,
    IUtcClock& clock
) noexcept
    : store_{store}, clock_{clock} {}

PersistedWorkflowState WorkflowStateService::create(
    const domain::Workflow& workflow,
    const WorkflowRetryPolicy& retry_policy,
    std::optional<domain::WorkflowBranchDecisionSnapshot> branch_decisions
) {
    auto checkpoint = initialize_workflow_checkpoint_manifest(
        workflow, retry_policy
    );
    require_identity(workflow, checkpoint, branch_decisions);

    PersistedWorkflowState state{
        .workflow = workflow,
        .checkpoint = std::move(checkpoint),
        .branch_decisions = std::move(branch_decisions),
        .revision = 0,
        .updated_at_utc = clock_.now_utc_iso8601(),
    };
    if (!store_.create(state)) {
        throw WorkflowStateServiceError{
            WorkflowStateServiceErrorCode::workflow_state_exists,
            "Workflow state already exists"
        };
    }
    return state;
}

PersistedWorkflowState WorkflowStateService::update(
    const std::string_view workflow_id,
    domain::WorkflowCheckpointManifest checkpoint,
    std::optional<domain::WorkflowBranchDecisionSnapshot> branch_decisions,
    const std::int64_t expected_revision
) {
    auto current = store_.find_by_workflow_id(workflow_id);
    if (!current.has_value()) {
        throw WorkflowStateServiceError{
            WorkflowStateServiceErrorCode::workflow_state_not_found,
            "Workflow state was not found"
        };
    }
    if (expected_revision < 0 ||
        current->revision != expected_revision) {
        throw WorkflowStateServiceError{
            WorkflowStateServiceErrorCode::concurrent_update,
            "Workflow state revision changed before update"
        };
    }
    if (expected_revision == std::numeric_limits<std::int64_t>::max()) {
        throw WorkflowStateServiceError{
            WorkflowStateServiceErrorCode::revision_overflow,
            "Workflow state revision cannot advance"
        };
    }

    require_identity(current->workflow, checkpoint, branch_decisions);

    PersistedWorkflowState next{
        .workflow = current->workflow,
        .checkpoint = std::move(checkpoint),
        .branch_decisions = std::move(branch_decisions),
        .revision = expected_revision + 1,
        .updated_at_utc = clock_.now_utc_iso8601(),
    };
    if (!store_.update(next, expected_revision)) {
        throw WorkflowStateServiceError{
            WorkflowStateServiceErrorCode::concurrent_update,
            "Workflow state changed before update could be persisted"
        };
    }
    return next;
}

std::optional<PersistedWorkflowState> WorkflowStateService::find(
    const std::string_view workflow_id
) {
    return store_.find_by_workflow_id(workflow_id);
}

}  // namespace biocore::application
