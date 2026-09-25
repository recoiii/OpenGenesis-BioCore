#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "biocore/domain/workflow.hpp"
#include "biocore/domain/workflow_branch_decision.hpp"

namespace biocore::application {

enum class WorkflowIntegerComparison {
    equal,
    not_equal,
};

enum class WorkflowNumericComparison {
    less,
    less_or_equal,
    equal,
    greater_or_equal,
    greater,
};

enum class WorkflowConditionStatus {
    pass,
    warning,
    fail,
};

[[nodiscard]] std::string_view to_string(WorkflowConditionStatus status) noexcept;

struct WorkflowArtifactPresencePredicate final {
    domain::WorkflowNodeId source_node_id;
    std::string output_port;
    bool expected_present{true};
};

struct WorkflowExitCodePredicate final {
    domain::WorkflowNodeId source_node_id;
    WorkflowIntegerComparison comparison{WorkflowIntegerComparison::equal};
    std::int64_t value{0};
};

struct WorkflowNumericMetricPredicate final {
    std::string metric_name;
    WorkflowNumericComparison comparison{WorkflowNumericComparison::greater_or_equal};
    double threshold{0.0};
};

struct WorkflowStatusPredicate final {
    std::string status_name;
    WorkflowConditionStatus expected{WorkflowConditionStatus::pass};
};

using WorkflowConditionPredicate = std::variant<
    WorkflowArtifactPresencePredicate,
    WorkflowExitCodePredicate,
    WorkflowNumericMetricPredicate,
    WorkflowStatusPredicate
>;

struct WorkflowConditionalNodeRule final {
    domain::WorkflowNodeId node_id;
    WorkflowConditionPredicate predicate;
};

struct WorkflowConditionalPolicy final {
    std::vector<WorkflowConditionalNodeRule> rules;
};

struct WorkflowNodeConditionObservation final {
    domain::WorkflowNodeId node_id;
    bool terminal{false};
    std::optional<std::int64_t> exit_code;
    std::vector<std::string> present_outputs;
};

struct WorkflowNumericMetricObservation final {
    std::string name;
    double value{0.0};
};

struct WorkflowStatusObservation final {
    std::string name;
    WorkflowConditionStatus value{WorkflowConditionStatus::pass};
};

struct WorkflowConditionContext final {
    std::vector<WorkflowNodeConditionObservation> nodes;
    std::vector<WorkflowNumericMetricObservation> numeric_metrics;
    std::vector<WorkflowStatusObservation> statuses;
};

enum class WorkflowConditionErrorCode {
    duplicate_rule,
    unknown_rule_node,
    unknown_condition_source_node,
    unknown_condition_output_port,
    self_condition_dependency,
    control_cycle,
    duplicate_node_observation,
    duplicate_metric_observation,
    duplicate_status_observation,
    invalid_metric_name,
    non_finite_metric,
};

[[nodiscard]] std::string_view to_string(WorkflowConditionErrorCode code) noexcept;

class WorkflowConditionError final : public std::invalid_argument {
public:
    WorkflowConditionError(WorkflowConditionErrorCode code, std::string message);

    [[nodiscard]] WorkflowConditionErrorCode code() const noexcept;

private:
    WorkflowConditionErrorCode code_;
};

[[nodiscard]] domain::WorkflowBranchDecisionSnapshot evaluate_workflow_conditions(
    const domain::Workflow& workflow,
    const WorkflowConditionalPolicy& policy,
    const WorkflowConditionContext& context
);

}  // namespace biocore::application
