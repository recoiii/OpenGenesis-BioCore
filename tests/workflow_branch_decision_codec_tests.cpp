#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "biocore/domain/workflow_branch_decision.hpp"
#include "biocore/pipeline_protocol/workflow_branch_decision_codec.hpp"

namespace {

using namespace biocore;

[[nodiscard]] domain::WorkflowBranchDecisionSnapshot snapshot() {
    return domain::WorkflowBranchDecisionSnapshot{
        domain::WorkflowBranchDecisionSnapshot::current_schema_version,
        domain::WorkflowId{"wf-decision"},
        {
            {
                domain::WorkflowNodeId{"a"},
                domain::WorkflowBranchDecisionState::selected,
                domain::WorkflowBranchDecisionReason::unconditional,
                std::nullopt
            },
            {
                domain::WorkflowNodeId{"b"},
                domain::WorkflowBranchDecisionState::skipped,
                domain::WorkflowBranchDecisionReason::condition_false,
                false
            },
            {
                domain::WorkflowNodeId{"c"},
                domain::WorkflowBranchDecisionState::deferred,
                domain::WorkflowBranchDecisionReason::condition_unresolved,
                std::nullopt
            },
        }
    };
}

[[nodiscard]] bool rejects(const std::string_view json) {
    try {
        static_cast<void>(
            pipeline_protocol::parse_workflow_branch_decision_snapshot(json)
        );
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

[[nodiscard]] bool round_trip_contract() {
    const auto original = snapshot();
    const std::string encoded =
        pipeline_protocol::serialize_workflow_branch_decision_snapshot(original);
    const auto decoded =
        pipeline_protocol::parse_workflow_branch_decision_snapshot(encoded);
    return decoded.workflow_id() == original.workflow_id() &&
           decoded.schema_version() == original.schema_version() &&
           decoded.decisions().size() == 3U &&
           decoded.decisions()[1].state ==
               domain::WorkflowBranchDecisionState::skipped &&
           decoded.decisions()[1].condition_result == std::optional<bool>{false};
}

[[nodiscard]] bool deterministic_contract() {
    const auto value = snapshot();
    const std::string first =
        pipeline_protocol::serialize_workflow_branch_decision_snapshot(value);
    const std::string second =
        pipeline_protocol::serialize_workflow_branch_decision_snapshot(value);
    const std::string third =
        pipeline_protocol::serialize_workflow_branch_decision_snapshot(
            pipeline_protocol::parse_workflow_branch_decision_snapshot(first)
        );
    return first == second && first == third;
}

[[nodiscard]] bool invalid_contract() {
    return rejects(
               R"({"schemaVersion":2,"workflowId":"wf-x","decisions":[]})"
           ) &&
           rejects(
               R"({"schemaVersion":1,"workflowId":"wf-x","decisions":[{"nodeId":"a","state":"selected","reason":"unconditional","conditionResult":null},{"nodeId":"a","state":"selected","reason":"unconditional","conditionResult":null}]})"
           ) &&
           rejects(
               R"({"schemaVersion":1,"workflowId":"wf-x","decisions":[{"nodeId":"a","state":"skipped","reason":"condition_false","conditionResult":true}]})"
           ) &&
           rejects(
               R"({"schemaVersion":1,"workflowId":"wf-x","decisions":[{"nodeId":"a","state":"selected","reason":"condition_false","conditionResult":false}]})"
           ) &&
           rejects(
               R"({"schemaVersion":1,"workflowId":"wf-x","decisions":[],"unknown":1})"
           );
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    const std::string_view name{argv[1]};
    bool passed = false;
    if (name == "round-trip") passed = round_trip_contract();
    else if (name == "deterministic") passed = deterministic_contract();
    else if (name == "invalid") passed = invalid_contract();
    else return EXIT_FAILURE;

    if (!passed) {
        std::cerr << "Workflow branch decision codec test failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Workflow branch decision codec test passed\n";
    return EXIT_SUCCESS;
}
