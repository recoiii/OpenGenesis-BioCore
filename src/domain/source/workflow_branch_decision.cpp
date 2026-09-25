#include "biocore/domain/workflow_branch_decision.hpp"

#include <set>
#include <utility>

namespace biocore::domain {

std::string_view to_string(const WorkflowBranchDecisionState state) noexcept {
    switch (state) {
        case WorkflowBranchDecisionState::selected: return "selected";
        case WorkflowBranchDecisionState::skipped: return "skipped";
        case WorkflowBranchDecisionState::blocked: return "blocked";
        case WorkflowBranchDecisionState::deferred: return "deferred";
    }
    return "unknown";
}

std::optional<WorkflowBranchDecisionState>
workflow_branch_decision_state_from_string(const std::string_view value) noexcept {
    if (value == "selected") return WorkflowBranchDecisionState::selected;
    if (value == "skipped") return WorkflowBranchDecisionState::skipped;
    if (value == "blocked") return WorkflowBranchDecisionState::blocked;
    if (value == "deferred") return WorkflowBranchDecisionState::deferred;
    return std::nullopt;
}

std::string_view to_string(const WorkflowBranchDecisionReason reason) noexcept {
    switch (reason) {
        case WorkflowBranchDecisionReason::unconditional: return "unconditional";
        case WorkflowBranchDecisionReason::condition_true: return "condition_true";
        case WorkflowBranchDecisionReason::condition_false: return "condition_false";
        case WorkflowBranchDecisionReason::condition_unresolved: return "condition_unresolved";
        case WorkflowBranchDecisionReason::required_input_unavailable:
            return "required_input_unavailable";
        case WorkflowBranchDecisionReason::condition_source_unavailable:
            return "condition_source_unavailable";
    }
    return "unknown";
}

std::optional<WorkflowBranchDecisionReason>
workflow_branch_decision_reason_from_string(const std::string_view value) noexcept {
    if (value == "unconditional") return WorkflowBranchDecisionReason::unconditional;
    if (value == "condition_true") return WorkflowBranchDecisionReason::condition_true;
    if (value == "condition_false") return WorkflowBranchDecisionReason::condition_false;
    if (value == "condition_unresolved") return WorkflowBranchDecisionReason::condition_unresolved;
    if (value == "required_input_unavailable") {
        return WorkflowBranchDecisionReason::required_input_unavailable;
    }
    if (value == "condition_source_unavailable") {
        return WorkflowBranchDecisionReason::condition_source_unavailable;
    }
    return std::nullopt;
}

WorkflowBranchDecisionSnapshot::WorkflowBranchDecisionSnapshot(
    const std::uint32_t schema_version,
    WorkflowId workflow_id,
    std::vector<WorkflowBranchDecision> decisions
)
    : schema_version_{schema_version},
      workflow_id_{std::move(workflow_id)},
      decisions_{std::move(decisions)} {
    if (schema_version_ != current_schema_version) {
        throw std::invalid_argument("Workflow branch decision schema version is unsupported");
    }

    std::set<std::string, std::less<>> node_ids;
    for (const WorkflowBranchDecision& decision : decisions_) {
        if (!node_ids.emplace(decision.node_id.value()).second) {
            throw std::invalid_argument(
                "Workflow branch decision snapshot contains duplicate node identifiers"
            );
        }
        const bool valid =
            (decision.state == WorkflowBranchDecisionState::selected &&
             decision.reason == WorkflowBranchDecisionReason::unconditional &&
             !decision.condition_result.has_value()) ||
            (decision.state == WorkflowBranchDecisionState::selected &&
             decision.reason == WorkflowBranchDecisionReason::condition_true &&
             decision.condition_result == std::optional<bool>{true}) ||
            (decision.state == WorkflowBranchDecisionState::skipped &&
             decision.reason == WorkflowBranchDecisionReason::condition_false &&
             decision.condition_result == std::optional<bool>{false}) ||
            (decision.state == WorkflowBranchDecisionState::deferred &&
             decision.reason == WorkflowBranchDecisionReason::condition_unresolved &&
             !decision.condition_result.has_value()) ||
            (decision.state == WorkflowBranchDecisionState::blocked &&
             decision.reason == WorkflowBranchDecisionReason::required_input_unavailable &&
             !decision.condition_result.has_value()) ||
            (decision.state == WorkflowBranchDecisionState::blocked &&
             decision.reason == WorkflowBranchDecisionReason::condition_source_unavailable &&
             !decision.condition_result.has_value());
        if (!valid) {
            throw std::invalid_argument(
                "Workflow branch decision state, reason, and condition result are inconsistent"
            );
        }
    }
}

std::uint32_t WorkflowBranchDecisionSnapshot::schema_version() const noexcept {
    return schema_version_;
}

const WorkflowId& WorkflowBranchDecisionSnapshot::workflow_id() const noexcept {
    return workflow_id_;
}

const std::vector<WorkflowBranchDecision>&
WorkflowBranchDecisionSnapshot::decisions() const noexcept {
    return decisions_;
}

}  // namespace biocore::domain
