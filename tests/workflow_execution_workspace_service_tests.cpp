#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/i_workflow_checkpoint_artifact_verifier.hpp"
#include "biocore/application/i_workflow_state_store.hpp"
#include "biocore/application/workflow_execution_workspace_service.hpp"
#include "biocore/application/workflow_state_service.hpp"
#include "biocore/domain/workflow.hpp"

namespace {

using namespace biocore;

class Clock final : public application::IUtcClock {
public:
    std::string now_utc_iso8601() override {
        ++tick_;
        return "2026-09-25T16:00:" +
               (tick_ < 10 ? std::string{"0"} : std::string{}) +
               std::to_string(tick_) + "Z";
    }

private:
    int tick_{0};
};

class Store final : public application::IWorkflowStateStore {
public:
    bool create(const application::PersistedWorkflowState& state) override {
        const std::string id{state.workflow.id().value()};
        return states_.emplace(id, state).second;
    }

    bool update(
        const application::PersistedWorkflowState& state,
        const std::int64_t expected_revision
    ) override {
        const std::string id{state.workflow.id().value()};
        const auto iterator = states_.find(id);
        if (iterator == states_.end() ||
            iterator->second.revision != expected_revision) {
            return false;
        }
        iterator->second = state;
        return true;
    }

    std::optional<application::PersistedWorkflowState> find_by_workflow_id(
        const std::string_view workflow_id
    ) override {
        const auto iterator = states_.find(workflow_id);
        if (iterator == states_.end()) return std::nullopt;
        return iterator->second;
    }

    std::vector<application::PersistedWorkflowState> list() override {
        std::vector<application::PersistedWorkflowState> result;
        result.reserve(states_.size());
        for (const auto& [id, state] : states_) {
            static_cast<void>(id);
            result.push_back(state);
        }
        return result;
    }

private:
    std::map<std::string, application::PersistedWorkflowState, std::less<>> states_;
};

class Verifier final
    : public application::IWorkflowCheckpointArtifactVerifier {
public:
    explicit Verifier(std::set<std::string, std::less<>> valid = {})
        : valid_{std::move(valid)} {}

    [[nodiscard]] bool verify(
        const domain::WorkflowCheckpointArtifact& artifact
    ) const override {
        return valid_.contains(artifact.relative_project_path);
    }

private:
    std::set<std::string, std::less<>> valid_;
};

[[nodiscard]] domain::Workflow workflow(std::string id) {
    return domain::Workflow{
        1U,
        domain::WorkflowId{std::move(id)},
        "Execution workspace",
        "Workspace test",
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
                domain::WorkflowNodeId{"source"},
                "result",
                domain::WorkflowNodeId{"target"},
                "input"
            }
        }
    };
}

[[nodiscard]] bool create_contract() {
    Store store;
    Clock clock;
    Verifier verifier;
    application::WorkflowStateService states{store, clock};
    application::WorkflowExecutionWorkspaceService workspace{
        states, verifier
    };

    const auto snapshot = workspace.create(
        workflow("org.biocore.workflow.exec-create")
    );
    const auto summaries = workspace.list();

    return snapshot.workflow_id == "org.biocore.workflow.exec-create" &&
           snapshot.revision == 0 &&
           snapshot.nodes.size() == 2U &&
           snapshot.nodes[0].checkpoint_state ==
               domain::WorkflowCheckpointNodeState::pending &&
           snapshot.nodes[0].resume_action ==
               application::WorkflowResumeActionKind::execute &&
           snapshot.nodes[0].next_attempt_number ==
               std::optional<std::uint32_t>{1U} &&
           summaries.size() == 1U &&
           summaries[0].pending_count == 2U &&
           summaries[0].node_count == 2U;
}

[[nodiscard]] bool completed_reuse_contract() {
    Store store;
    Clock clock;
    Verifier verifier{{"outputs/source.txt"}};
    application::WorkflowStateService states{store, clock};
    application::WorkflowExecutionWorkspaceService workspace{
        states, verifier
    };

    const auto created = workspace.create(
        workflow("org.biocore.workflow.exec-reuse")
    );
    domain::WorkflowCheckpointManifest checkpoint{
        1U,
        domain::WorkflowId{"org.biocore.workflow.exec-reuse"},
        {
            {
                domain::WorkflowNodeId{"source"},
                domain::WorkflowCheckpointNodeState::completed,
                1U,
                3U,
                {
                    {
                        "result",
                        "outputs/source.txt",
                        3,
                        std::string(64U, 'a')
                    }
                },
                std::nullopt,
            },
            {
                domain::WorkflowNodeId{"target"},
                domain::WorkflowCheckpointNodeState::pending,
                0U,
                3U,
                {},
                std::nullopt,
            },
        }
    };
    static_cast<void>(states.update(
        created.workflow_id,
        std::move(checkpoint),
        std::nullopt,
        created.revision
    ));

    const auto snapshot = workspace.find(created.workflow_id);
    return snapshot.has_value() &&
           snapshot->revision == 1 &&
           snapshot->nodes[0].resume_action ==
               application::WorkflowResumeActionKind::reuse_completed &&
           snapshot->nodes[0].resume_reason ==
               application::WorkflowResumeReason::completed_verified &&
           snapshot->nodes[1].resume_action ==
               application::WorkflowResumeActionKind::execute;
}

[[nodiscard]] bool branch_overlay_contract() {
    Store store;
    Clock clock;
    Verifier verifier;
    application::WorkflowStateService states{store, clock};
    application::WorkflowExecutionWorkspaceService workspace{
        states, verifier
    };

    const auto created = workspace.create(
        workflow("org.biocore.workflow.exec-branch")
    );

    domain::WorkflowCheckpointManifest checkpoint{
        1U,
        domain::WorkflowId{"org.biocore.workflow.exec-branch"},
        {
            {
                domain::WorkflowNodeId{"source"},
                domain::WorkflowCheckpointNodeState::skipped,
                0U, 3U, {}, std::nullopt
            },
            {
                domain::WorkflowNodeId{"target"},
                domain::WorkflowCheckpointNodeState::blocked,
                0U, 3U, {}, std::nullopt
            },
        }
    };
    domain::WorkflowBranchDecisionSnapshot decisions{
        1U,
        domain::WorkflowId{"org.biocore.workflow.exec-branch"},
        {
            {
                domain::WorkflowNodeId{"source"},
                domain::WorkflowBranchDecisionState::skipped,
                domain::WorkflowBranchDecisionReason::condition_false,
                false
            },
            {
                domain::WorkflowNodeId{"target"},
                domain::WorkflowBranchDecisionState::blocked,
                domain::WorkflowBranchDecisionReason::required_input_unavailable,
                std::nullopt
            },
        }
    };
    static_cast<void>(states.update(
        created.workflow_id,
        std::move(checkpoint),
        std::move(decisions),
        created.revision
    ));

    const auto snapshot = workspace.find(created.workflow_id);
    return snapshot.has_value() &&
           snapshot->nodes[0].branch_state ==
               std::optional<domain::WorkflowBranchDecisionState>{
                   domain::WorkflowBranchDecisionState::skipped
               } &&
           snapshot->nodes[0].resume_action ==
               application::WorkflowResumeActionKind::preserve_skipped &&
           snapshot->nodes[1].branch_reason ==
               std::optional<domain::WorkflowBranchDecisionReason>{
                   domain::WorkflowBranchDecisionReason::required_input_unavailable
               } &&
           snapshot->nodes[1].resume_action ==
               application::WorkflowResumeActionKind::preserve_blocked;
}

[[nodiscard]] bool deterministic_list_contract() {
    Store store;
    Clock clock;
    Verifier verifier;
    application::WorkflowStateService states{store, clock};
    application::WorkflowExecutionWorkspaceService workspace{
        states, verifier
    };

    static_cast<void>(workspace.create(
        workflow("org.biocore.workflow.zzz")
    ));
    static_cast<void>(workspace.create(
        workflow("org.biocore.workflow.aaa")
    ));

    const auto values = workspace.list();
    return values.size() == 2U &&
           values[0].workflow_id == "org.biocore.workflow.aaa" &&
           values[1].workflow_id == "org.biocore.workflow.zzz";
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    const std::string_view mode{argv[1]};

    bool passed = false;
    if (mode == "create") passed = create_contract();
    else if (mode == "completed-reuse") passed = completed_reuse_contract();
    else if (mode == "branch-overlay") passed = branch_overlay_contract();
    else if (mode == "deterministic-list") passed = deterministic_list_contract();
    else return EXIT_FAILURE;

    if (!passed) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
