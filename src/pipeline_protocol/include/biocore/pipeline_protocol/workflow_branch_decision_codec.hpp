#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "biocore/domain/workflow_branch_decision.hpp"

namespace biocore::pipeline_protocol {

inline constexpr std::size_t maximum_workflow_branch_decision_document_bytes =
    1024U * 1024U;

[[nodiscard]] std::string serialize_workflow_branch_decision_snapshot(
    const biocore::domain::WorkflowBranchDecisionSnapshot& snapshot
);

[[nodiscard]] biocore::domain::WorkflowBranchDecisionSnapshot
parse_workflow_branch_decision_snapshot(std::string_view json);

}  // namespace biocore::pipeline_protocol
