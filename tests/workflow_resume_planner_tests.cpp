#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/workflow_resume_planner.hpp"
#include "biocore/domain/workflow.hpp"

namespace {

using namespace biocore;

class FakeVerifier final
    : public application::IWorkflowCheckpointArtifactVerifier {
public:
    explicit FakeVerifier(std::set<std::string, std::less<>> valid)
        : valid_{std::move(valid)} {}

    [[nodiscard]] bool verify(
        const domain::WorkflowCheckpointArtifact& artifact
    ) const override {
        return valid_.contains(artifact.relative_project_path);
    }

private:
    std::set<std::string, std::less<>> valid_;
};

[[nodiscard]] std::string sha() {
    return std::string(64U, 'a');
}

[[nodiscard]] domain::Workflow workflow(const bool optional_middle = false) {
    return domain::Workflow{
        domain::Workflow::current_schema_version,
        domain::WorkflowId{"wf-resume"},
        "Resume",
        "",
        {
            domain::WorkflowNode{
                domain::WorkflowNodeId{"source"},
                "Source",
                "org.biocore.test.source",
                "1.0.0",
                {},
                {domain::WorkflowOutputDeclaration{"result", "txt"}},
                {}
            },
            domain::WorkflowNode{
                domain::WorkflowNodeId{"middle"},
                "Middle",
                "org.biocore.test.middle",
                "1.0.0",
                {domain::WorkflowInputDeclaration{"input", "txt", !optional_middle}},
                {domain::WorkflowOutputDeclaration{"result", "txt"}},
                {}
            },
            domain::WorkflowNode{
                domain::WorkflowNodeId{"target"},
                "Target",
                "org.biocore.test.target",
                "1.0.0",
                {domain::WorkflowInputDeclaration{"input", "txt", true}},
                {domain::WorkflowOutputDeclaration{"report", "json"}},
                {}
            },
        },
        {
            domain::WorkflowEdge{
                domain::WorkflowNodeId{"source"}, "result",
                domain::WorkflowNodeId{"middle"}, "input"
            },
            domain::WorkflowEdge{
                domain::WorkflowNodeId{"middle"}, "result",
                domain::WorkflowNodeId{"target"}, "input"
            },
        }
    };
}

[[nodiscard]] domain::WorkflowCheckpointArtifact artifact(
    std::string port,
    std::string path
) {
    return domain::WorkflowCheckpointArtifact{
        std::move(port), std::move(path), 3, sha()
    };
}

[[nodiscard]] domain::WorkflowNodeCheckpoint pending(
    std::string id,
    const std::uint32_t max_attempts = 3U
) {
    return {
        domain::WorkflowNodeId{std::move(id)},
        domain::WorkflowCheckpointNodeState::pending,
        0U,
        max_attempts,
        {},
        std::nullopt,
    };
}

[[nodiscard]] domain::WorkflowNodeCheckpoint completed(
    std::string id,
    std::string port,
    std::string path,
    const std::uint32_t attempt = 1U,
    const std::uint32_t max_attempts = 3U
) {
    return {
        domain::WorkflowNodeId{std::move(id)},
        domain::WorkflowCheckpointNodeState::completed,
        attempt,
        max_attempts,
        {artifact(std::move(port), std::move(path))},
        std::nullopt,
    };
}

[[nodiscard]] domain::WorkflowCheckpointManifest manifest(
    std::vector<domain::WorkflowNodeCheckpoint> nodes,
    std::string workflow_id = "wf-resume"
) {
    return {
        domain::WorkflowCheckpointManifest::current_schema_version,
        domain::WorkflowId{std::move(workflow_id)},
        std::move(nodes),
    };
}

[[nodiscard]] const application::WorkflowNodeResumeAction* find_action(
    const application::WorkflowResumePlan& plan,
    const std::string_view id
) {
    const auto iterator = std::ranges::find_if(
        plan.actions(),
        [id](const auto& action) { return action.node_id.value() == id; }
    );
    return iterator == plan.actions().end() ? nullptr : &*iterator;
}

[[nodiscard]] bool throws_code(
    auto&& function,
    const application::WorkflowResumeErrorCode code
) {
    try {
        function();
    } catch (const application::WorkflowResumeError& error) {
        return error.code() == code;
    }
    return false;
}

[[nodiscard]] bool initialize_contract() {
    const application::WorkflowRetryPolicy policy{
        3U,
        {{domain::WorkflowNodeId{"middle"}, 5U}}
    };
    const auto value =
        application::initialize_workflow_checkpoint_manifest(workflow(), policy);
    return value.nodes().size() == 3U &&
           value.nodes()[0].node_id.value() == "source" &&
           value.nodes()[0].max_attempts == 3U &&
           value.nodes()[1].node_id.value() == "middle" &&
           value.nodes()[1].max_attempts == 5U &&
           value.nodes()[2].node_id.value() == "target";
}

[[nodiscard]] bool reuse_contract() {
    const auto value = manifest({
        completed("source", "result", "source.txt"),
        completed("middle", "result", "middle.txt"),
        completed("target", "report", "target.json"),
    });
    const FakeVerifier verifier{{"source.txt", "middle.txt", "target.json"}};
    const auto plan =
        application::plan_workflow_resume(workflow(), value, verifier);
    return std::ranges::all_of(plan.actions(), [](const auto& action) {
        return action.action ==
               application::WorkflowResumeActionKind::reuse_completed;
    });
}

[[nodiscard]] bool corrupt_contract() {
    const auto value = manifest({
        completed("source", "result", "source.txt"),
        pending("middle"),
        pending("target"),
    });
    const FakeVerifier verifier{{}};
    const auto plan =
        application::plan_workflow_resume(workflow(), value, verifier);
    const auto* source = find_action(plan, "source");
    return source != nullptr &&
           source->action == application::WorkflowResumeActionKind::retry &&
           source->reason ==
               application::WorkflowResumeReason::artifact_integrity_failed &&
           source->next_attempt_number == std::optional<std::uint32_t>{2U};
}

[[nodiscard]] bool cascade_contract() {
    const auto value = manifest({
        completed("source", "result", "source.txt"),
        completed("middle", "result", "middle.txt"),
        completed("target", "report", "target.json"),
    });
    const FakeVerifier verifier{{"middle.txt", "target.json"}};
    const auto plan =
        application::plan_workflow_resume(workflow(), value, verifier);
    const auto* source = find_action(plan, "source");
    const auto* middle = find_action(plan, "middle");
    const auto* target = find_action(plan, "target");
    return source != nullptr && middle != nullptr && target != nullptr &&
           source->reason ==
               application::WorkflowResumeReason::artifact_integrity_failed &&
           middle->reason == application::WorkflowResumeReason::upstream_recomputed &&
           target->reason == application::WorkflowResumeReason::upstream_recomputed &&
           source->action == application::WorkflowResumeActionKind::retry &&
           middle->action == application::WorkflowResumeActionKind::retry &&
           target->action == application::WorkflowResumeActionKind::retry;
}

[[nodiscard]] bool missing_output_contract() {
    auto source = completed("source", "result", "source.txt");
    source.outputs.clear();
    const auto value = manifest({
        std::move(source),
        pending("middle"),
        pending("target"),
    });
    const FakeVerifier verifier{{"source.txt"}};
    const auto plan =
        application::plan_workflow_resume(workflow(), value, verifier);
    const auto* action = find_action(plan, "source");
    return action != nullptr &&
           action->reason ==
               application::WorkflowResumeReason::artifact_integrity_failed;
}

[[nodiscard]] bool failed_contract() {
    auto source = pending("source");
    source.state = domain::WorkflowCheckpointNodeState::failed;
    source.attempt_number = 1U;
    source.failure = domain::WorkflowCheckpointFailure{"boom", 2};
    const auto value = manifest({
        std::move(source), pending("middle"), pending("target")
    });
    const FakeVerifier verifier{{}};
    const auto plan =
        application::plan_workflow_resume(workflow(), value, verifier);
    const auto* action = find_action(plan, "source");
    return action != nullptr &&
           action->action == application::WorkflowResumeActionKind::retry &&
           action->reason == application::WorkflowResumeReason::failed_retry &&
           action->next_attempt_number == std::optional<std::uint32_t>{2U} &&
           action->previous_failure.has_value() &&
           action->previous_failure->message == "boom";
}

[[nodiscard]] bool interrupted_contract() {
    auto source = pending("source");
    source.state = domain::WorkflowCheckpointNodeState::interrupted;
    source.attempt_number = 1U;
    source.failure = domain::WorkflowCheckpointFailure{"interrupted", std::nullopt};
    const auto value = manifest({
        std::move(source), pending("middle"), pending("target")
    });
    const FakeVerifier verifier{{}};
    const auto plan =
        application::plan_workflow_resume(workflow(), value, verifier);
    const auto* action = find_action(plan, "source");
    return action != nullptr &&
           action->reason == application::WorkflowResumeReason::interrupted_retry;
}

[[nodiscard]] bool running_contract() {
    auto source = pending("source");
    source.state = domain::WorkflowCheckpointNodeState::running;
    source.attempt_number = 1U;
    const auto value = manifest({
        std::move(source), pending("middle"), pending("target")
    });
    const FakeVerifier verifier{{}};
    const auto plan =
        application::plan_workflow_resume(workflow(), value, verifier);
    const auto* action = find_action(plan, "source");
    return action != nullptr &&
           action->reason == application::WorkflowResumeReason::incomplete_retry &&
           action->action == application::WorkflowResumeActionKind::retry;
}

[[nodiscard]] bool exhausted_contract() {
    auto source = pending("source", 2U);
    source.state = domain::WorkflowCheckpointNodeState::failed;
    source.attempt_number = 2U;
    source.failure = domain::WorkflowCheckpointFailure{"boom", 1};
    const auto value = manifest({
        std::move(source), pending("middle"), pending("target")
    });
    const FakeVerifier verifier{{}};
    const auto plan =
        application::plan_workflow_resume(workflow(), value, verifier);
    const auto* action = find_action(plan, "source");
    return action != nullptr &&
           action->action == application::WorkflowResumeActionKind::exhausted &&
           action->reason ==
               application::WorkflowResumeReason::attempt_limit_reached;
}

[[nodiscard]] bool required_block_contract() {
    auto source = pending("source");
    source.state = domain::WorkflowCheckpointNodeState::skipped;
    const auto value = manifest({
        std::move(source), pending("middle"), pending("target")
    });
    const FakeVerifier verifier{{}};
    const auto plan =
        application::plan_workflow_resume(workflow(), value, verifier);
    const auto* middle = find_action(plan, "middle");
    const auto* target = find_action(plan, "target");
    return middle != nullptr && target != nullptr &&
           middle->action ==
               application::WorkflowResumeActionKind::preserve_blocked &&
           middle->reason ==
               application::WorkflowResumeReason::dependency_unavailable &&
           target->action ==
               application::WorkflowResumeActionKind::preserve_blocked;
}

[[nodiscard]] bool optional_continue_contract() {
    auto source = pending("source");
    source.state = domain::WorkflowCheckpointNodeState::skipped;
    const auto value = manifest({
        std::move(source), pending("middle"), pending("target")
    });
    const FakeVerifier verifier{{}};
    const auto plan =
        application::plan_workflow_resume(workflow(true), value, verifier);
    const auto* middle = find_action(plan, "middle");
    return middle != nullptr &&
           middle->action == application::WorkflowResumeActionKind::execute &&
           middle->next_attempt_number == std::optional<std::uint32_t>{1U};
}

[[nodiscard]] bool reevaluate_contract() {
    auto middle = pending("middle");
    middle.state = domain::WorkflowCheckpointNodeState::skipped;
    const auto value = manifest({
        completed("source", "result", "source.txt"),
        std::move(middle),
        pending("target"),
    });
    const FakeVerifier verifier{{}};
    const auto plan =
        application::plan_workflow_resume(workflow(), value, verifier);
    const auto* source = find_action(plan, "source");
    const auto* middle_action = find_action(plan, "middle");
    return source != nullptr && middle_action != nullptr &&
           source->action == application::WorkflowResumeActionKind::retry &&
           middle_action->action ==
               application::WorkflowResumeActionKind::reevaluate_condition &&
           middle_action->reason ==
               application::WorkflowResumeReason::upstream_recomputed;
}

[[nodiscard]] bool manifest_errors_contract() {
    const FakeVerifier verifier{{}};
    return throws_code(
               [&] {
                   static_cast<void>(application::plan_workflow_resume(
                       workflow(),
                       manifest({
                           pending("source"), pending("middle"), pending("target")
                       }, "wf-other"),
                       verifier
                   ));
               },
               application::WorkflowResumeErrorCode::workflow_id_mismatch
           ) &&
           throws_code(
               [&] {
                   static_cast<void>(application::plan_workflow_resume(
                       workflow(),
                       manifest({pending("source"), pending("middle")}),
                       verifier
                   ));
               },
               application::WorkflowResumeErrorCode::missing_checkpoint_node
           ) &&
           throws_code(
               [&] {
                   static_cast<void>(application::plan_workflow_resume(
                       workflow(),
                       manifest({
                           pending("source"), pending("middle"), pending("target"),
                           pending("extra")
                       }),
                       verifier
                   ));
               },
               application::WorkflowResumeErrorCode::unknown_checkpoint_node
           );
}

[[nodiscard]] bool policy_errors_contract() {
    return throws_code(
               [&] {
                   static_cast<void>(
                       application::initialize_workflow_checkpoint_manifest(
                           workflow(),
                           application::WorkflowRetryPolicy{0U, {}}
                       )
                   );
               },
               application::WorkflowResumeErrorCode::invalid_retry_policy
           ) &&
           throws_code(
               [&] {
                   static_cast<void>(
                       application::initialize_workflow_checkpoint_manifest(
                           workflow(),
                           application::WorkflowRetryPolicy{
                               3U,
                               {
                                   {domain::WorkflowNodeId{"source"}, 2U},
                                   {domain::WorkflowNodeId{"source"}, 3U},
                               }
                           }
                       )
                   );
               },
               application::WorkflowResumeErrorCode::duplicate_retry_override
           ) &&
           throws_code(
               [&] {
                   static_cast<void>(
                       application::initialize_workflow_checkpoint_manifest(
                           workflow(),
                           application::WorkflowRetryPolicy{
                               3U,
                               {{domain::WorkflowNodeId{"missing"}, 2U}}
                           }
                       )
                   );
               },
               application::WorkflowResumeErrorCode::unknown_retry_override_node
           );
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    const std::string_view name{argv[1]};
    bool passed = false;
    if (name == "initialize") passed = initialize_contract();
    else if (name == "reuse") passed = reuse_contract();
    else if (name == "corrupt") passed = corrupt_contract();
    else if (name == "cascade") passed = cascade_contract();
    else if (name == "missing-output") passed = missing_output_contract();
    else if (name == "failed") passed = failed_contract();
    else if (name == "interrupted") passed = interrupted_contract();
    else if (name == "running") passed = running_contract();
    else if (name == "exhausted") passed = exhausted_contract();
    else if (name == "required-block") passed = required_block_contract();
    else if (name == "optional-continue") passed = optional_continue_contract();
    else if (name == "reevaluate") passed = reevaluate_contract();
    else if (name == "manifest-errors") passed = manifest_errors_contract();
    else if (name == "policy-errors") passed = policy_errors_contract();
    else return EXIT_FAILURE;

    if (!passed) {
        std::cerr << "Workflow resume planner test failed: " << name << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Workflow resume planner test passed: " << name << '\n';
    return EXIT_SUCCESS;
}
