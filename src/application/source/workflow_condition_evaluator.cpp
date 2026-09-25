#include "biocore/application/workflow_condition_evaluator.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <type_traits>
#include <utility>

#include "biocore/domain/workflow_dag.hpp"

namespace biocore::application {
namespace {

using NodeLookup = std::map<std::string, const domain::WorkflowNode*, std::less<>>;
using DependencyMap = std::map<std::string, std::set<std::string, std::less<>>, std::less<>>;
using RuleMap = std::map<std::string, const WorkflowConditionalNodeRule*, std::less<>>;
using DecisionMap = std::map<std::string, domain::WorkflowBranchDecision, std::less<>>;

[[nodiscard]] bool valid_name(
    const std::string_view value,
    const std::size_t maximum_length = 128U
) noexcept {
    if (value.empty() || value.size() > maximum_length ||
        value.find('\0') != std::string_view::npos ||
        !(value.front() >= 'a' && value.front() <= 'z')) {
        return false;
    }
    return std::ranges::all_of(value, [](const char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') ||
               character == '-' || character == '_' || character == '.';
    });
}

[[noreturn]] void fail(
    const WorkflowConditionErrorCode code,
    const std::string& message
) {
    throw WorkflowConditionError{code, "Workflow condition evaluation failed: " + message};
}

[[nodiscard]] const domain::WorkflowInputDeclaration* find_input(
    const domain::WorkflowNode& node,
    const std::string_view name
) noexcept {
    const auto iterator = std::ranges::find_if(
        node.inputs(), [name](const auto& input) { return input.name() == name; }
    );
    return iterator == node.inputs().end() ? nullptr : &*iterator;
}

[[nodiscard]] const domain::WorkflowOutputDeclaration* find_output(
    const domain::WorkflowNode& node,
    const std::string_view name
) noexcept {
    const auto iterator = std::ranges::find_if(
        node.outputs(), [name](const auto& output) { return output.name() == name; }
    );
    return iterator == node.outputs().end() ? nullptr : &*iterator;
}

[[nodiscard]] std::optional<std::string> condition_source_node(
    const WorkflowConditionPredicate& predicate
) {
    return std::visit(
        [](const auto& typed) -> std::optional<std::string> {
            using Predicate = std::decay_t<decltype(typed)>;
            if constexpr (
                std::is_same_v<Predicate, WorkflowArtifactPresencePredicate> ||
                std::is_same_v<Predicate, WorkflowExitCodePredicate>
            ) {
                return std::string{typed.source_node_id.value()};
            }
            return std::nullopt;
        },
        predicate
    );
}

struct Evaluation final {
    bool resolved{false};
    bool result{false};
};

[[nodiscard]] bool compare_integer(
    const std::int64_t observed,
    const WorkflowIntegerComparison comparison,
    const std::int64_t expected
) noexcept {
    switch (comparison) {
        case WorkflowIntegerComparison::equal: return observed == expected;
        case WorkflowIntegerComparison::not_equal: return observed != expected;
        default: return false;
    }
}

[[nodiscard]] bool compare_numeric(
    const double observed,
    const WorkflowNumericComparison comparison,
    const double threshold
) noexcept {
    switch (comparison) {
        case WorkflowNumericComparison::less: return observed < threshold;
        case WorkflowNumericComparison::less_or_equal: return observed <= threshold;
        case WorkflowNumericComparison::equal: return observed == threshold;
        case WorkflowNumericComparison::greater_or_equal: return observed >= threshold;
        case WorkflowNumericComparison::greater: return observed > threshold;
        default: return false;
    }
}

}  // namespace

std::string_view to_string(const WorkflowConditionStatus status) noexcept {
    switch (status) {
        case WorkflowConditionStatus::pass: return "pass";
        case WorkflowConditionStatus::warning: return "warning";
        case WorkflowConditionStatus::fail: return "fail";
    }
    return "unknown";
}

std::string_view to_string(const WorkflowConditionErrorCode code) noexcept {
    switch (code) {
        case WorkflowConditionErrorCode::duplicate_rule: return "duplicate_rule";
        case WorkflowConditionErrorCode::unknown_rule_node: return "unknown_rule_node";
        case WorkflowConditionErrorCode::unknown_condition_source_node:
            return "unknown_condition_source_node";
        case WorkflowConditionErrorCode::unknown_condition_output_port:
            return "unknown_condition_output_port";
        case WorkflowConditionErrorCode::self_condition_dependency:
            return "self_condition_dependency";
        case WorkflowConditionErrorCode::control_cycle: return "control_cycle";
        case WorkflowConditionErrorCode::duplicate_node_observation:
            return "duplicate_node_observation";
        case WorkflowConditionErrorCode::duplicate_metric_observation:
            return "duplicate_metric_observation";
        case WorkflowConditionErrorCode::duplicate_status_observation:
            return "duplicate_status_observation";
        case WorkflowConditionErrorCode::invalid_metric_name: return "invalid_metric_name";
        case WorkflowConditionErrorCode::non_finite_metric: return "non_finite_metric";
    }
    return "unknown";
}

WorkflowConditionError::WorkflowConditionError(
    const WorkflowConditionErrorCode code,
    std::string message
)
    : std::invalid_argument{std::move(message)}, code_{code} {}

WorkflowConditionErrorCode WorkflowConditionError::code() const noexcept { return code_; }

domain::WorkflowBranchDecisionSnapshot evaluate_workflow_conditions(
    const domain::Workflow& workflow,
    const WorkflowConditionalPolicy& policy,
    const WorkflowConditionContext& context
) {
    static_cast<void>(domain::plan_workflow_dag(workflow));

    NodeLookup nodes;
    DependencyMap dependencies;
    std::map<std::string, std::set<std::string, std::less<>>, std::less<>> dependents;
    for (const domain::WorkflowNode& node : workflow.nodes()) {
        const std::string id{node.id().value()};
        nodes.emplace(id, &node);
        dependencies.emplace(id, std::set<std::string, std::less<>>{});
        dependents.emplace(id, std::set<std::string, std::less<>>{});
    }
    for (const domain::WorkflowEdge& edge : workflow.edges()) {
        const std::string source{edge.source_node_id().value()};
        const std::string target{edge.target_node_id().value()};
        dependencies.at(target).insert(source);
        dependents.at(source).insert(target);
    }

    RuleMap rules;
    for (const WorkflowConditionalNodeRule& rule : policy.rules) {
        const std::string node_id{rule.node_id.value()};
        if (!nodes.contains(node_id)) {
            fail(
                WorkflowConditionErrorCode::unknown_rule_node,
                "rule targets unknown node '" + node_id + "'"
            );
        }
        if (!rules.emplace(node_id, &rule).second) {
            fail(
                WorkflowConditionErrorCode::duplicate_rule,
                "node '" + node_id + "' has more than one conditional rule"
            );
        }

        const auto source = condition_source_node(rule.predicate);
        if (source.has_value()) {
            if (!nodes.contains(*source)) {
                fail(
                    WorkflowConditionErrorCode::unknown_condition_source_node,
                    "condition references unknown source node '" + *source + "'"
                );
            }
            if (*source == node_id) {
                fail(
                    WorkflowConditionErrorCode::self_condition_dependency,
                    "node '" + node_id + "' cannot condition itself"
                );
            }
            dependencies.at(node_id).insert(*source);
            dependents.at(*source).insert(node_id);
        }

        if (const auto* artifact =
                std::get_if<WorkflowArtifactPresencePredicate>(&rule.predicate)) {
            const domain::WorkflowNode& source_node = *nodes.at(
                std::string{artifact->source_node_id.value()}
            );
            if (find_output(source_node, artifact->output_port) == nullptr) {
                fail(
                    WorkflowConditionErrorCode::unknown_condition_output_port,
                    "artifact-presence condition references unknown output '" +
                        std::string{artifact->source_node_id.value()} + "." +
                        artifact->output_port + "'"
                );
            }
        }

        if (const auto* numeric =
                std::get_if<WorkflowNumericMetricPredicate>(&rule.predicate)) {
            if (!valid_name(numeric->metric_name) || !std::isfinite(numeric->threshold)) {
                fail(
                    WorkflowConditionErrorCode::invalid_metric_name,
                    "numeric predicate is invalid"
                );
            }
        }
        if (const auto* status =
                std::get_if<WorkflowStatusPredicate>(&rule.predicate)) {
            if (!valid_name(status->status_name)) {
                fail(
                    WorkflowConditionErrorCode::invalid_metric_name,
                    "status predicate name is invalid"
                );
            }
        }
    }

    std::map<std::string, std::size_t, std::less<>> indegrees;
    std::set<std::string, std::less<>> ready;
    for (const auto& [id, deps] : dependencies) {
        indegrees.emplace(id, deps.size());
        if (deps.empty()) ready.insert(id);
    }

    std::vector<std::string> evaluation_order;
    while (!ready.empty()) {
        const std::string id = *ready.begin();
        ready.erase(ready.begin());
        evaluation_order.push_back(id);
        for (const std::string& dependent : dependents.at(id)) {
            auto& indegree = indegrees.at(dependent);
            if (indegree == 0U) {
                throw std::logic_error(
                    "Workflow conditional planner encountered an invalid indegree"
                );
            }
            --indegree;
            if (indegree == 0U) ready.insert(dependent);
        }
    }
    if (evaluation_order.size() != nodes.size()) {
        fail(
            WorkflowConditionErrorCode::control_cycle,
            "conditional control dependencies introduce a cycle"
        );
    }

    std::map<std::string, const WorkflowNodeConditionObservation*, std::less<>>
        node_observations;
    for (const WorkflowNodeConditionObservation& observation : context.nodes) {
        const std::string id{observation.node_id.value()};
        if (!node_observations.emplace(id, &observation).second) {
            fail(
                WorkflowConditionErrorCode::duplicate_node_observation,
                "duplicate node observation for '" + id + "'"
            );
        }
    }

    std::map<std::string, double, std::less<>> numeric_metrics;
    for (const WorkflowNumericMetricObservation& metric : context.numeric_metrics) {
        if (!valid_name(metric.name)) {
            fail(
                WorkflowConditionErrorCode::invalid_metric_name,
                "numeric metric name is invalid"
            );
        }
        if (!std::isfinite(metric.value)) {
            fail(
                WorkflowConditionErrorCode::non_finite_metric,
                "numeric metric '" + metric.name + "' is not finite"
            );
        }
        if (!numeric_metrics.emplace(metric.name, metric.value).second) {
            fail(
                WorkflowConditionErrorCode::duplicate_metric_observation,
                "duplicate numeric metric '" + metric.name + "'"
            );
        }
    }

    std::map<std::string, WorkflowConditionStatus, std::less<>> statuses;
    for (const WorkflowStatusObservation& status : context.statuses) {
        if (!valid_name(status.name)) {
            fail(
                WorkflowConditionErrorCode::invalid_metric_name,
                "status observation name is invalid"
            );
        }
        if (!statuses.emplace(status.name, status.value).second) {
            fail(
                WorkflowConditionErrorCode::duplicate_status_observation,
                "duplicate status observation '" + status.name + "'"
            );
        }
    }

    const auto evaluate_predicate = [&](const WorkflowConditionPredicate& predicate) {
        return std::visit(
            [&](const auto& typed) -> Evaluation {
                using Predicate = std::decay_t<decltype(typed)>;
                if constexpr (std::is_same_v<Predicate, WorkflowArtifactPresencePredicate>) {
                    const std::string source{typed.source_node_id.value()};
                    const auto iterator = node_observations.find(source);
                    if (iterator == node_observations.end()) return {};
                    const auto& observation = *iterator->second;
                    const bool present =
                        std::ranges::find(observation.present_outputs, typed.output_port) !=
                        observation.present_outputs.end();
                    if (present) return Evaluation{true, typed.expected_present};
                    if (!observation.terminal) return {};
                    return Evaluation{true, !typed.expected_present};
                } else if constexpr (std::is_same_v<Predicate, WorkflowExitCodePredicate>) {
                    const std::string source{typed.source_node_id.value()};
                    const auto iterator = node_observations.find(source);
                    if (iterator == node_observations.end() ||
                        !iterator->second->terminal ||
                        !iterator->second->exit_code.has_value()) {
                        return {};
                    }
                    return Evaluation{
                        true,
                        compare_integer(
                            *iterator->second->exit_code,
                            typed.comparison,
                            typed.value
                        )
                    };
                } else if constexpr (
                    std::is_same_v<Predicate, WorkflowNumericMetricPredicate>
                ) {
                    const auto iterator = numeric_metrics.find(typed.metric_name);
                    if (iterator == numeric_metrics.end()) return {};
                    return Evaluation{
                        true,
                        compare_numeric(iterator->second, typed.comparison, typed.threshold)
                    };
                } else {
                    const auto iterator = statuses.find(typed.status_name);
                    if (iterator == statuses.end()) return {};
                    return Evaluation{true, iterator->second == typed.expected};
                }
            },
            predicate
        );
    };

    std::map<std::string, std::vector<const domain::WorkflowEdge*>, std::less<>>
        incoming_edges;
    for (const domain::WorkflowEdge& edge : workflow.edges()) {
        incoming_edges[std::string{edge.target_node_id().value()}].push_back(&edge);
    }

    DecisionMap decisions;
    std::vector<domain::WorkflowBranchDecision> ordered_decisions;
    ordered_decisions.reserve(evaluation_order.size());

    for (const std::string& node_id : evaluation_order) {
        const domain::WorkflowNode& node = *nodes.at(node_id);

        bool required_input_blocked = false;
        bool required_input_deferred = false;
        for (const domain::WorkflowEdge* edge : incoming_edges[node_id]) {
            const auto source_decision =
                decisions.find(std::string{edge->source_node_id().value()});
            if (source_decision == decisions.end()) continue;
            const auto* input = find_input(node, edge->target_input());
            if (input == nullptr || !input->required()) continue;

            if (source_decision->second.state ==
                    domain::WorkflowBranchDecisionState::skipped ||
                source_decision->second.state ==
                    domain::WorkflowBranchDecisionState::blocked) {
                required_input_blocked = true;
                break;
            }
            if (source_decision->second.state ==
                domain::WorkflowBranchDecisionState::deferred) {
                required_input_deferred = true;
            }
        }

        if (required_input_blocked) {
            domain::WorkflowBranchDecision decision{
                domain::WorkflowNodeId{node_id},
                domain::WorkflowBranchDecisionState::blocked,
                domain::WorkflowBranchDecisionReason::required_input_unavailable,
                std::nullopt,
            };
            decisions.emplace(node_id, decision);
            ordered_decisions.push_back(std::move(decision));
            continue;
        }
        if (required_input_deferred) {
            domain::WorkflowBranchDecision decision{
                domain::WorkflowNodeId{node_id},
                domain::WorkflowBranchDecisionState::deferred,
                domain::WorkflowBranchDecisionReason::condition_unresolved,
                std::nullopt,
            };
            decisions.emplace(node_id, decision);
            ordered_decisions.push_back(std::move(decision));
            continue;
        }

        const auto rule = rules.find(node_id);
        if (rule == rules.end()) {
            domain::WorkflowBranchDecision decision{
                domain::WorkflowNodeId{node_id},
                domain::WorkflowBranchDecisionState::selected,
                domain::WorkflowBranchDecisionReason::unconditional,
                std::nullopt,
            };
            decisions.emplace(node_id, decision);
            ordered_decisions.push_back(std::move(decision));
            continue;
        }

        const auto source = condition_source_node(rule->second->predicate);
        if (source.has_value()) {
            const auto source_decision = decisions.find(*source);
            if (source_decision != decisions.end() &&
                (source_decision->second.state ==
                     domain::WorkflowBranchDecisionState::skipped ||
                 source_decision->second.state ==
                     domain::WorkflowBranchDecisionState::blocked)) {
                domain::WorkflowBranchDecision decision{
                    domain::WorkflowNodeId{node_id},
                    domain::WorkflowBranchDecisionState::blocked,
                    domain::WorkflowBranchDecisionReason::condition_source_unavailable,
                    std::nullopt,
                };
                decisions.emplace(node_id, decision);
                ordered_decisions.push_back(std::move(decision));
                continue;
            }
            if (source_decision != decisions.end() &&
                source_decision->second.state ==
                    domain::WorkflowBranchDecisionState::deferred) {
                domain::WorkflowBranchDecision decision{
                    domain::WorkflowNodeId{node_id},
                    domain::WorkflowBranchDecisionState::deferred,
                    domain::WorkflowBranchDecisionReason::condition_unresolved,
                    std::nullopt,
                };
                decisions.emplace(node_id, decision);
                ordered_decisions.push_back(std::move(decision));
                continue;
            }
        }

        const Evaluation evaluated = evaluate_predicate(rule->second->predicate);
        domain::WorkflowBranchDecision decision{
            domain::WorkflowNodeId{node_id},
            evaluated.resolved
                ? (evaluated.result
                    ? domain::WorkflowBranchDecisionState::selected
                    : domain::WorkflowBranchDecisionState::skipped)
                : domain::WorkflowBranchDecisionState::deferred,
            evaluated.resolved
                ? (evaluated.result
                    ? domain::WorkflowBranchDecisionReason::condition_true
                    : domain::WorkflowBranchDecisionReason::condition_false)
                : domain::WorkflowBranchDecisionReason::condition_unresolved,
            evaluated.resolved ? std::optional<bool>{evaluated.result} : std::nullopt,
        };
        decisions.emplace(node_id, decision);
        ordered_decisions.push_back(std::move(decision));
    }

    return domain::WorkflowBranchDecisionSnapshot{
        domain::WorkflowBranchDecisionSnapshot::current_schema_version,
        workflow.id(),
        std::move(ordered_decisions),
    };
}

}  // namespace biocore::application
