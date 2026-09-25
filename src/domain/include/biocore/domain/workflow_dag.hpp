#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/domain/workflow.hpp"

namespace biocore::domain {

enum class WorkflowDagErrorCode {
    empty_workflow,
    missing_source_node,
    missing_target_node,
    missing_source_output,
    missing_target_input,
    self_edge,
    duplicate_edge,
    multiple_incoming_connections,
    cycle,
};

[[nodiscard]] std::string_view to_string(WorkflowDagErrorCode code) noexcept;

class WorkflowDagError final : public std::invalid_argument {
public:
    WorkflowDagError(WorkflowDagErrorCode code, std::string message);

    [[nodiscard]] WorkflowDagErrorCode code() const noexcept;

private:
    WorkflowDagErrorCode code_;
};

struct WorkflowPlanNode final {
    WorkflowNodeId id;
    std::vector<WorkflowNodeId> depends_on;
};

class WorkflowDagPlan final {
public:
    WorkflowDagPlan(
        std::vector<WorkflowPlanNode> ordered_nodes,
        std::vector<std::vector<WorkflowNodeId>> execution_stages
    );

    [[nodiscard]] const std::vector<WorkflowPlanNode>& ordered_nodes() const noexcept;
    [[nodiscard]] const std::vector<std::vector<WorkflowNodeId>>& execution_stages() const noexcept;

private:
    std::vector<WorkflowPlanNode> ordered_nodes_;
    std::vector<std::vector<WorkflowNodeId>> execution_stages_;
};

[[nodiscard]] WorkflowDagPlan plan_workflow_dag(const Workflow& workflow);

}  // namespace biocore::domain
