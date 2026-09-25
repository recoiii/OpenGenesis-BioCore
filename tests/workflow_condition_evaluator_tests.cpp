#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/workflow_condition_evaluator.hpp"
#include "biocore/domain/workflow.hpp"

namespace {

using namespace biocore;

[[nodiscard]] domain::WorkflowNode make_node(
    std::string id,
    std::vector<domain::WorkflowInputDeclaration> inputs = {},
    std::vector<domain::WorkflowOutputDeclaration> outputs = {}
) {
    return domain::WorkflowNode{
        domain::WorkflowNodeId{std::move(id)},
        "Node",
        "org.biocore.test.node",
        "1.0.0",
        std::move(inputs),
        std::move(outputs),
        {},
    };
}

[[nodiscard]] domain::Workflow base_workflow(bool optional_target = false) {
    return domain::Workflow{
        domain::Workflow::current_schema_version,
        domain::WorkflowId{"wf-conditional"},
        "Conditional",
        "",
        {
            make_node("source", {}, {domain::WorkflowOutputDeclaration{"result", "txt"}}),
            make_node(
                "target",
                {domain::WorkflowInputDeclaration{"input", "txt", !optional_target}},
                {domain::WorkflowOutputDeclaration{"report", "json"}}
            ),
            make_node("success"),
            make_node("failure"),
        },
        {
            domain::WorkflowEdge{
                domain::WorkflowNodeId{"source"}, "result",
                domain::WorkflowNodeId{"target"}, "input"
            },
        },
    };
}

[[nodiscard]] const domain::WorkflowBranchDecision* find_decision(
    const domain::WorkflowBranchDecisionSnapshot& snapshot,
    const std::string_view id
) {
    const auto iterator = std::ranges::find_if(
        snapshot.decisions(),
        [id](const auto& decision) { return decision.node_id.value() == id; }
    );
    return iterator == snapshot.decisions().end() ? nullptr : &*iterator;
}

[[nodiscard]] bool throws_code(
    const domain::Workflow& workflow,
    const application::WorkflowConditionalPolicy& policy,
    const application::WorkflowConditionContext& context,
    const application::WorkflowConditionErrorCode code
) {
    try {
        static_cast<void>(
            application::evaluate_workflow_conditions(workflow, policy, context)
        );
    } catch (const application::WorkflowConditionError& error) {
        return error.code() == code;
    }
    return false;
}

[[nodiscard]] bool numeric_contract() {
    const application::WorkflowConditionalPolicy policy{{
        {
            domain::WorkflowNodeId{"target"},
            application::WorkflowNumericMetricPredicate{
                "coverage.mean",
                application::WorkflowNumericComparison::greater_or_equal,
                30.0
            }
        }
    }};
    const application::WorkflowConditionContext context{
        {},
        {{"coverage.mean", 31.5}},
        {}
    };
    const auto snapshot =
        application::evaluate_workflow_conditions(base_workflow(), policy, context);
    const auto* decision = find_decision(snapshot, "target");
    return decision != nullptr &&
           decision->state == domain::WorkflowBranchDecisionState::selected &&
           decision->reason == domain::WorkflowBranchDecisionReason::condition_true &&
           decision->condition_result == std::optional<bool>{true};
}

[[nodiscard]] bool status_contract() {
    const application::WorkflowConditionalPolicy policy{{
        {
            domain::WorkflowNodeId{"target"},
            application::WorkflowStatusPredicate{
                "qc.status",
                application::WorkflowConditionStatus::pass
            }
        }
    }};
    const application::WorkflowConditionContext context{
        {},
        {},
        {{"qc.status", application::WorkflowConditionStatus::fail}}
    };
    const auto snapshot =
        application::evaluate_workflow_conditions(base_workflow(), policy, context);
    const auto* decision = find_decision(snapshot, "target");
    return decision != nullptr &&
           decision->state == domain::WorkflowBranchDecisionState::skipped &&
           decision->reason == domain::WorkflowBranchDecisionReason::condition_false;
}

[[nodiscard]] bool artifact_contract() {
    const application::WorkflowConditionalPolicy policy{{
        {
            domain::WorkflowNodeId{"success"},
            application::WorkflowArtifactPresencePredicate{
                domain::WorkflowNodeId{"source"}, "result", true
            }
        }
    }};
    const application::WorkflowConditionContext context{
        {
            {
                domain::WorkflowNodeId{"source"},
                true,
                std::int64_t{0},
                {"result"}
            }
        },
        {},
        {}
    };
    const auto snapshot =
        application::evaluate_workflow_conditions(base_workflow(), policy, context);
    const auto* decision = find_decision(snapshot, "success");
    return decision != nullptr &&
           decision->state == domain::WorkflowBranchDecisionState::selected;
}

[[nodiscard]] bool exit_branch_contract() {
    const application::WorkflowConditionalPolicy policy{{
        {
            domain::WorkflowNodeId{"success"},
            application::WorkflowExitCodePredicate{
                domain::WorkflowNodeId{"source"},
                application::WorkflowIntegerComparison::equal,
                0
            }
        },
        {
            domain::WorkflowNodeId{"failure"},
            application::WorkflowExitCodePredicate{
                domain::WorkflowNodeId{"source"},
                application::WorkflowIntegerComparison::not_equal,
                0
            }
        }
    }};
    const application::WorkflowConditionContext context{
        {
            {domain::WorkflowNodeId{"source"}, true, std::int64_t{2}, {}}
        },
        {},
        {}
    };
    const auto snapshot =
        application::evaluate_workflow_conditions(base_workflow(), policy, context);
    const auto* success = find_decision(snapshot, "success");
    const auto* failure = find_decision(snapshot, "failure");
    return success != nullptr && failure != nullptr &&
           success->state == domain::WorkflowBranchDecisionState::skipped &&
           failure->state == domain::WorkflowBranchDecisionState::selected;
}

[[nodiscard]] bool deferred_contract() {
    const application::WorkflowConditionalPolicy policy{{
        {
            domain::WorkflowNodeId{"success"},
            application::WorkflowExitCodePredicate{
                domain::WorkflowNodeId{"source"},
                application::WorkflowIntegerComparison::equal,
                0
            }
        }
    }};
    const auto snapshot = application::evaluate_workflow_conditions(
        base_workflow(), policy, {}
    );
    const auto* decision = find_decision(snapshot, "success");
    return decision != nullptr &&
           decision->state == domain::WorkflowBranchDecisionState::deferred &&
           decision->reason == domain::WorkflowBranchDecisionReason::condition_unresolved;
}

[[nodiscard]] bool required_block_contract() {
    const application::WorkflowConditionalPolicy policy{{
        {
            domain::WorkflowNodeId{"source"},
            application::WorkflowStatusPredicate{
                "gate.status",
                application::WorkflowConditionStatus::pass
            }
        }
    }};
    const application::WorkflowConditionContext context{
        {},
        {},
        {{"gate.status", application::WorkflowConditionStatus::fail}}
    };
    const auto snapshot =
        application::evaluate_workflow_conditions(base_workflow(), policy, context);
    const auto* source = find_decision(snapshot, "source");
    const auto* target = find_decision(snapshot, "target");
    return source != nullptr && target != nullptr &&
           source->state == domain::WorkflowBranchDecisionState::skipped &&
           target->state == domain::WorkflowBranchDecisionState::blocked &&
           target->reason ==
               domain::WorkflowBranchDecisionReason::required_input_unavailable;
}

[[nodiscard]] bool optional_skip_contract() {
    const application::WorkflowConditionalPolicy policy{{
        {
            domain::WorkflowNodeId{"source"},
            application::WorkflowStatusPredicate{
                "gate.status",
                application::WorkflowConditionStatus::pass
            }
        }
    }};
    const application::WorkflowConditionContext context{
        {},
        {},
        {{"gate.status", application::WorkflowConditionStatus::fail}}
    };
    const auto snapshot =
        application::evaluate_workflow_conditions(base_workflow(true), policy, context);
    const auto* target = find_decision(snapshot, "target");
    return target != nullptr &&
           target->state == domain::WorkflowBranchDecisionState::selected;
}

[[nodiscard]] bool source_block_contract() {
    const application::WorkflowConditionalPolicy policy{{
        {
            domain::WorkflowNodeId{"source"},
            application::WorkflowStatusPredicate{
                "source.gate",
                application::WorkflowConditionStatus::pass
            }
        },
        {
            domain::WorkflowNodeId{"success"},
            application::WorkflowExitCodePredicate{
                domain::WorkflowNodeId{"source"},
                application::WorkflowIntegerComparison::equal,
                0
            }
        }
    }};
    const application::WorkflowConditionContext context{
        {},
        {},
        {{"source.gate", application::WorkflowConditionStatus::fail}}
    };
    const auto snapshot =
        application::evaluate_workflow_conditions(base_workflow(), policy, context);
    const auto* success = find_decision(snapshot, "success");
    return success != nullptr &&
           success->state == domain::WorkflowBranchDecisionState::blocked &&
           success->reason ==
               domain::WorkflowBranchDecisionReason::condition_source_unavailable;
}

[[nodiscard]] bool control_cycle_contract() {
    const application::WorkflowConditionalPolicy policy{{
        {
            domain::WorkflowNodeId{"source"},
            application::WorkflowExitCodePredicate{
                domain::WorkflowNodeId{"target"},
                application::WorkflowIntegerComparison::equal,
                0
            }
        }
    }};
    return throws_code(
        base_workflow(),
        policy,
        {},
        application::WorkflowConditionErrorCode::control_cycle
    );
}

[[nodiscard]] bool duplicate_rule_contract() {
    const application::WorkflowConditionalPolicy policy{{
        {
            domain::WorkflowNodeId{"target"},
            application::WorkflowStatusPredicate{
                "qc.status", application::WorkflowConditionStatus::pass
            }
        },
        {
            domain::WorkflowNodeId{"target"},
            application::WorkflowStatusPredicate{
                "qc.other", application::WorkflowConditionStatus::pass
            }
        }
    }};
    return throws_code(
        base_workflow(),
        policy,
        {},
        application::WorkflowConditionErrorCode::duplicate_rule
    );
}

[[nodiscard]] bool deterministic_contract() {
    const auto first = base_workflow();
    const auto second = base_workflow();
    const application::WorkflowConditionalPolicy first_policy{{
        {
            domain::WorkflowNodeId{"success"},
            application::WorkflowExitCodePredicate{
                domain::WorkflowNodeId{"source"},
                application::WorkflowIntegerComparison::equal,
                0
            }
        },
        {
            domain::WorkflowNodeId{"failure"},
            application::WorkflowStatusPredicate{
                "qc.status", application::WorkflowConditionStatus::fail
            }
        }
    }};
    auto second_policy = first_policy;
    std::reverse(second_policy.rules.begin(), second_policy.rules.end());

    const application::WorkflowConditionContext context{
        {{domain::WorkflowNodeId{"source"}, true, std::int64_t{0}, {"result"}}},
        {},
        {{"qc.status", application::WorkflowConditionStatus::pass}}
    };
    const auto left =
        application::evaluate_workflow_conditions(first, first_policy, context);
    const auto right =
        application::evaluate_workflow_conditions(second, second_policy, context);

    if (left.decisions().size() != right.decisions().size()) return false;
    for (std::size_t index = 0U; index < left.decisions().size(); ++index) {
        const auto& a = left.decisions()[index];
        const auto& b = right.decisions()[index];
        if (a.node_id != b.node_id || a.state != b.state ||
            a.reason != b.reason || a.condition_result != b.condition_result) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool invalid_context_contract() {
    const application::WorkflowConditionalPolicy policy{};
    const application::WorkflowConditionContext duplicate_metric{
        {},
        {{"coverage.mean", 1.0}, {"coverage.mean", 2.0}},
        {}
    };
    const application::WorkflowConditionContext non_finite{
        {},
        {{"coverage.mean", std::numeric_limits<double>::infinity()}},
        {}
    };
    return throws_code(
               base_workflow(),
               policy,
               duplicate_metric,
               application::WorkflowConditionErrorCode::duplicate_metric_observation
           ) &&
           throws_code(
               base_workflow(),
               policy,
               non_finite,
               application::WorkflowConditionErrorCode::non_finite_metric
           );
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    const std::string_view name{argv[1]};
    bool passed = false;
    if (name == "numeric") passed = numeric_contract();
    else if (name == "status") passed = status_contract();
    else if (name == "artifact") passed = artifact_contract();
    else if (name == "exit-branch") passed = exit_branch_contract();
    else if (name == "deferred") passed = deferred_contract();
    else if (name == "required-block") passed = required_block_contract();
    else if (name == "optional-skip") passed = optional_skip_contract();
    else if (name == "source-block") passed = source_block_contract();
    else if (name == "control-cycle") passed = control_cycle_contract();
    else if (name == "duplicate-rule") passed = duplicate_rule_contract();
    else if (name == "deterministic") passed = deterministic_contract();
    else if (name == "invalid-context") passed = invalid_context_contract();
    else return EXIT_FAILURE;

    if (!passed) {
        std::cerr << "Workflow condition test failed: " << name << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Workflow condition test passed: " << name << '\n';
    return EXIT_SUCCESS;
}
