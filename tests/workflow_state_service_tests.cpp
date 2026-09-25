#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/i_workflow_state_store.hpp"
#include "biocore/application/workflow_state_recovery_service.hpp"
#include "biocore/application/workflow_state_service.hpp"
#include "biocore/domain/workflow.hpp"

namespace {

using namespace biocore;

class Clock final : public application::IUtcClock {
public:
    std::string now_utc_iso8601() override {
        ++tick_;
        return "2026-09-25T12:00:" + (tick_ < 10 ? std::string{"0"} : std::string{}) +
               std::to_string(tick_) + "Z";
    }

private:
    int tick_{0};
};

class Store final : public application::IWorkflowStateStore {
public:
    bool create(const application::PersistedWorkflowState& state) override {
        if (state_.has_value()) return false;
        state_ = state;
        return true;
    }

    bool update(
        const application::PersistedWorkflowState& state,
        const std::int64_t expected_revision
    ) override {
        if (fail_updates_ || !state_.has_value() ||
            state_->revision != expected_revision) {
            return false;
        }
        state_ = state;
        return true;
    }

    std::optional<application::PersistedWorkflowState> find_by_workflow_id(
        const std::string_view workflow_id
    ) override {
        if (!state_.has_value() || state_->workflow.id().value() != workflow_id) {
            return std::nullopt;
        }
        return state_;
    }

    std::vector<application::PersistedWorkflowState> list() override {
        if (!state_.has_value()) return {};
        return {*state_};
    }

    void set(application::PersistedWorkflowState state) {
        state_ = std::move(state);
    }

    void fail_updates(const bool value) noexcept {
        fail_updates_ = value;
    }

private:
    std::optional<application::PersistedWorkflowState> state_;
    bool fail_updates_{false};
};

class Verifier final
    : public application::IWorkflowCheckpointArtifactVerifier {
public:
    [[nodiscard]] bool verify(
        const domain::WorkflowCheckpointArtifact&
    ) const override {
        return true;
    }
};

[[nodiscard]] domain::Workflow workflow() {
    return domain::Workflow{
        domain::Workflow::current_schema_version,
        domain::WorkflowId{"wf-state"},
        "State",
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

[[nodiscard]] domain::WorkflowBranchDecisionSnapshot decisions() {
    return domain::WorkflowBranchDecisionSnapshot{
        domain::WorkflowBranchDecisionSnapshot::current_schema_version,
        domain::WorkflowId{"wf-state"},
        {
            {
                domain::WorkflowNodeId{"source"},
                domain::WorkflowBranchDecisionState::selected,
                domain::WorkflowBranchDecisionReason::unconditional,
                std::nullopt
            },
            {
                domain::WorkflowNodeId{"target"},
                domain::WorkflowBranchDecisionState::deferred,
                domain::WorkflowBranchDecisionReason::condition_unresolved,
                std::nullopt
            },
        }
    };
}

[[nodiscard]] domain::WorkflowCheckpointManifest running_manifest() {
    return domain::WorkflowCheckpointManifest{
        domain::WorkflowCheckpointManifest::current_schema_version,
        domain::WorkflowId{"wf-state"},
        {
            {
                domain::WorkflowNodeId{"source"},
                domain::WorkflowCheckpointNodeState::running,
                1U,
                3U,
                {},
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
}

[[nodiscard]] bool throws_code(
    auto&& operation,
    const application::WorkflowStateServiceErrorCode code
) {
    try {
        operation();
    } catch (const application::WorkflowStateServiceError& error) {
        return error.code() == code;
    }
    return false;
}

[[nodiscard]] bool create_update_contract() {
    Store store;
    Clock clock;
    application::WorkflowStateService service{store, clock};

    const auto created = service.create(
        workflow(),
        application::WorkflowRetryPolicy{
            3U,
            {{domain::WorkflowNodeId{"target"}, 5U}}
        },
        decisions()
    );
    if (created.revision != 0 ||
        created.checkpoint.nodes().size() != 2U ||
        created.checkpoint.nodes()[1].max_attempts != 5U ||
        !created.branch_decisions.has_value()) {
        return false;
    }

    const auto updated = service.update(
        "wf-state",
        running_manifest(),
        decisions(),
        0
    );
    const auto found = service.find("wf-state");
    return updated.revision == 1 && found.has_value() &&
           found->revision == 1 &&
           found->checkpoint.nodes()[0].state ==
               domain::WorkflowCheckpointNodeState::running;
}

[[nodiscard]] bool conflict_contract() {
    Store store;
    Clock clock;
    application::WorkflowStateService service{store, clock};
    static_cast<void>(service.create(
        workflow(), application::WorkflowRetryPolicy{}, std::nullopt
    ));

    const bool duplicate = throws_code(
        [&] {
            static_cast<void>(service.create(
                workflow(), application::WorkflowRetryPolicy{}, std::nullopt
            ));
        },
        application::WorkflowStateServiceErrorCode::workflow_state_exists
    );

    const bool stale = throws_code(
        [&] {
            static_cast<void>(service.update(
                "wf-state",
                running_manifest(),
                std::nullopt,
                7
            ));
        },
        application::WorkflowStateServiceErrorCode::concurrent_update
    );

    return duplicate && stale;
}

[[nodiscard]] bool identity_contract() {
    Store store;
    Clock clock;
    application::WorkflowStateService service{store, clock};
    static_cast<void>(service.create(
        workflow(), application::WorkflowRetryPolicy{}, std::nullopt
    ));

    const domain::WorkflowCheckpointManifest wrong{
        domain::WorkflowCheckpointManifest::current_schema_version,
        domain::WorkflowId{"wf-other"},
        {
            {
                domain::WorkflowNodeId{"source"},
                domain::WorkflowCheckpointNodeState::pending,
                0U, 3U, {}, std::nullopt
            },
            {
                domain::WorkflowNodeId{"target"},
                domain::WorkflowCheckpointNodeState::pending,
                0U, 3U, {}, std::nullopt
            },
        }
    };

    return throws_code(
        [&] {
            static_cast<void>(service.update(
                "wf-state", wrong, std::nullopt, 0
            ));
        },
        application::WorkflowStateServiceErrorCode::workflow_identity_mismatch
    );
}

[[nodiscard]] bool recovery_running_contract() {
    Store store;
    Clock clock;
    Verifier verifier;
    store.set(application::PersistedWorkflowState{
        .workflow = workflow(),
        .checkpoint = running_manifest(),
        .branch_decisions = decisions(),
        .revision = 4,
        .updated_at_utc = "2026-09-25T11:00:00Z",
    });

    application::WorkflowStateRecoveryService recovery{
        store, verifier, clock
    };
    const auto result = recovery.recover();
    const auto stored = store.find_by_workflow_id("wf-state");
    if (result.recovered.size() != 1U || !result.issues.empty() ||
        !stored.has_value() || stored->revision != 5 ||
        stored->checkpoint.nodes()[0].state !=
            domain::WorkflowCheckpointNodeState::interrupted ||
        !stored->checkpoint.nodes()[0].failure.has_value()) {
        return false;
    }

    const auto& actions = result.recovered[0].resume_plan.actions();
    return result.recovered[0].reconciled_running_checkpoint &&
           actions.size() == 2U &&
           actions[0].action ==
               application::WorkflowResumeActionKind::retry &&
           actions[0].next_attempt_number ==
               std::optional<std::uint32_t>{2U};
}

[[nodiscard]] bool recovery_idempotent_contract() {
    Store store;
    Clock clock;
    Verifier verifier;
    store.set(application::PersistedWorkflowState{
        .workflow = workflow(),
        .checkpoint = running_manifest(),
        .branch_decisions = std::nullopt,
        .revision = 1,
        .updated_at_utc = "2026-09-25T11:00:00Z",
    });

    application::WorkflowStateRecoveryService recovery{
        store, verifier, clock
    };
    const auto first = recovery.recover();
    const auto second = recovery.recover();
    const auto stored = store.find_by_workflow_id("wf-state");
    return first.recovered.size() == 1U &&
           second.recovered.size() == 1U &&
           stored.has_value() &&
           stored->revision == 2 &&
           first.recovered[0].reconciled_running_checkpoint &&
           !second.recovered[0].reconciled_running_checkpoint;
}

[[nodiscard]] bool recovery_concurrent_contract() {
    Store store;
    Clock clock;
    Verifier verifier;
    store.set(application::PersistedWorkflowState{
        .workflow = workflow(),
        .checkpoint = running_manifest(),
        .branch_decisions = std::nullopt,
        .revision = 1,
        .updated_at_utc = "2026-09-25T11:00:00Z",
    });
    store.fail_updates(true);

    application::WorkflowStateRecoveryService recovery{
        store, verifier, clock
    };
    const auto result = recovery.recover();
    return result.recovered.empty() &&
           result.issues.size() == 1U &&
           result.issues[0].stage ==
               application::WorkflowStateRecoveryIssueStage::
                   persist_reconciled_state;
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    const std::string_view name{argv[1]};
    bool passed = false;
    if (name == "create-update") passed = create_update_contract();
    else if (name == "conflict") passed = conflict_contract();
    else if (name == "identity") passed = identity_contract();
    else if (name == "recovery-running") passed = recovery_running_contract();
    else if (name == "recovery-idempotent") passed = recovery_idempotent_contract();
    else if (name == "recovery-concurrent") passed = recovery_concurrent_contract();
    else return EXIT_FAILURE;

    if (!passed) {
        std::cerr << "Workflow state application test failed: " << name << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Workflow state application test passed: " << name << '\n';
    return EXIT_SUCCESS;
}
