#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/domain/workflow.hpp"

namespace biocore::domain {

enum class WorkflowBranchDecisionState {
    selected,
    skipped,
    blocked,
    deferred,
};

[[nodiscard]] std::string_view to_string(WorkflowBranchDecisionState state) noexcept;
[[nodiscard]] std::optional<WorkflowBranchDecisionState>
workflow_branch_decision_state_from_string(std::string_view value) noexcept;

enum class WorkflowBranchDecisionReason {
    unconditional,
    condition_true,
    condition_false,
    condition_unresolved,
    required_input_unavailable,
    condition_source_unavailable,
};

[[nodiscard]] std::string_view to_string(WorkflowBranchDecisionReason reason) noexcept;
[[nodiscard]] std::optional<WorkflowBranchDecisionReason>
workflow_branch_decision_reason_from_string(std::string_view value) noexcept;

struct WorkflowBranchDecision final {
    WorkflowNodeId node_id;
    WorkflowBranchDecisionState state{WorkflowBranchDecisionState::deferred};
    WorkflowBranchDecisionReason reason{WorkflowBranchDecisionReason::condition_unresolved};
    std::optional<bool> condition_result;
};

class WorkflowBranchDecisionSnapshot final {
public:
    static constexpr std::uint32_t current_schema_version = 1U;

    WorkflowBranchDecisionSnapshot(
        std::uint32_t schema_version,
        WorkflowId workflow_id,
        std::vector<WorkflowBranchDecision> decisions
    );

    [[nodiscard]] std::uint32_t schema_version() const noexcept;
    [[nodiscard]] const WorkflowId& workflow_id() const noexcept;
    [[nodiscard]] const std::vector<WorkflowBranchDecision>& decisions() const noexcept;

private:
    std::uint32_t schema_version_;
    WorkflowId workflow_id_;
    std::vector<WorkflowBranchDecision> decisions_;
};

}  // namespace biocore::domain
