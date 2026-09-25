#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_workflow_state_store.hpp"
#include "biocore/application/workflow_resume_planner.hpp"

namespace biocore::application {

class IUtcClock;

enum class WorkflowStateServiceErrorCode {
    workflow_state_exists,
    workflow_state_not_found,
    workflow_identity_mismatch,
    concurrent_update,
    revision_overflow,
};

[[nodiscard]] std::string_view to_string(WorkflowStateServiceErrorCode code) noexcept;

class WorkflowStateServiceError final : public std::runtime_error {
public:
    WorkflowStateServiceError(
        WorkflowStateServiceErrorCode code,
        std::string message
    );

    [[nodiscard]] WorkflowStateServiceErrorCode code() const noexcept;

private:
    WorkflowStateServiceErrorCode code_;
};

class WorkflowStateService final {
public:
    WorkflowStateService(
        IWorkflowStateStore& store,
        IUtcClock& clock
    ) noexcept;

    [[nodiscard]] PersistedWorkflowState create(
        const domain::Workflow& workflow,
        const WorkflowRetryPolicy& retry_policy,
        std::optional<domain::WorkflowBranchDecisionSnapshot> branch_decisions =
            std::nullopt
    );

    [[nodiscard]] PersistedWorkflowState update(
        std::string_view workflow_id,
        domain::WorkflowCheckpointManifest checkpoint,
        std::optional<domain::WorkflowBranchDecisionSnapshot> branch_decisions,
        std::int64_t expected_revision
    );

    [[nodiscard]] std::optional<PersistedWorkflowState> find(
        std::string_view workflow_id
    );

    [[nodiscard]] std::vector<PersistedWorkflowState> list();

private:
    IWorkflowStateStore& store_;
    IUtcClock& clock_;
};

}  // namespace biocore::application
