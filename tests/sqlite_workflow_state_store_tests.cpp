#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/i_workflow_state_store.hpp"
#include "biocore/domain/workflow.hpp"
#include "biocore/domain/workflow_branch_decision.hpp"
#include "biocore/domain/workflow_checkpoint.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_workflow_state_store.hpp"

namespace {

using namespace biocore;
namespace fs = std::filesystem;

class TempDatabase final {
public:
    explicit TempDatabase(const std::string_view suffix) {
        root_ = fs::temp_directory_path() /
            ("biocore-workflow-state-" + std::string{suffix} + "-" +
             std::to_string(
                 std::chrono::steady_clock::now().time_since_epoch().count()
             ));
        fs::create_directories(root_);
    }

    ~TempDatabase() {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    [[nodiscard]] fs::path path() const {
        return root_ / "project.sqlite";
    }

private:
    fs::path root_;
};

[[nodiscard]] domain::Workflow workflow(
    std::string id = "wf-state",
    std::string name = "Workflow state"
) {
    return domain::Workflow{
        domain::Workflow::current_schema_version,
        domain::WorkflowId{std::move(id)},
        std::move(name),
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

[[nodiscard]] domain::WorkflowCheckpointManifest checkpoint(
    std::string id = "wf-state"
) {
    return domain::WorkflowCheckpointManifest{
        domain::WorkflowCheckpointManifest::current_schema_version,
        domain::WorkflowId{std::move(id)},
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
                domain::WorkflowCheckpointNodeState::failed,
                2U,
                4U,
                {},
                domain::WorkflowCheckpointFailure{
                    "target failed",
                    std::int64_t{7}
                },
            },
        }
    };
}

[[nodiscard]] domain::WorkflowBranchDecisionSnapshot decisions(
    std::string id = "wf-state"
) {
    return domain::WorkflowBranchDecisionSnapshot{
        domain::WorkflowBranchDecisionSnapshot::current_schema_version,
        domain::WorkflowId{std::move(id)},
        {
            {
                domain::WorkflowNodeId{"source"},
                domain::WorkflowBranchDecisionState::selected,
                domain::WorkflowBranchDecisionReason::unconditional,
                std::nullopt
            },
            {
                domain::WorkflowNodeId{"target"},
                domain::WorkflowBranchDecisionState::skipped,
                domain::WorkflowBranchDecisionReason::condition_false,
                false
            },
        }
    };
}

[[nodiscard]] application::PersistedWorkflowState state(
    std::string id = "wf-state",
    std::int64_t revision = 0,
    std::string name = "Workflow state"
) {
    return application::PersistedWorkflowState{
        .workflow = workflow(id, std::move(name)),
        .checkpoint = checkpoint(id),
        .branch_decisions = decisions(id),
        .revision = revision,
        .updated_at_utc = "2026-09-25T12:00:00Z",
    };
}

void migrate(infrastructure::sqlite::SqliteConnection& connection) {
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    if (migrations.current_version() !=
        infrastructure::sqlite::latest_project_schema_version) {
        throw std::runtime_error("workflow state test requires current schema");
    }
}

[[nodiscard]] bool round_trip_contract() {
    TempDatabase db{"roundtrip"};
    infrastructure::sqlite::SqliteConnection connection{db.path()};
    migrate(connection);
    infrastructure::sqlite::SqliteWorkflowStateStore store{connection};

    const auto original = state();
    if (!store.create(original)) return false;
    const auto loaded = store.find_by_workflow_id("wf-state");
    if (!loaded.has_value()) return false;

    return loaded->revision == 0 &&
           loaded->workflow.name() == "Workflow state" &&
           loaded->checkpoint.nodes().size() == 2U &&
           loaded->checkpoint.nodes()[0].node_id.value() == "source" &&
           loaded->checkpoint.nodes()[0].outputs.size() == 1U &&
           loaded->checkpoint.nodes()[0].outputs[0].sha256 ==
               std::string(64U, 'a') &&
           loaded->checkpoint.nodes()[1].failure.has_value() &&
           loaded->checkpoint.nodes()[1].failure->exit_code ==
               std::optional<std::int64_t>{7} &&
           loaded->branch_decisions.has_value() &&
           loaded->branch_decisions->decisions().size() == 2U &&
           loaded->branch_decisions->decisions()[1].condition_result ==
               std::optional<bool>{false};
}

[[nodiscard]] bool update_contract() {
    TempDatabase db{"update"};
    infrastructure::sqlite::SqliteConnection connection{db.path()};
    migrate(connection);
    infrastructure::sqlite::SqliteWorkflowStateStore store{connection};

    if (!store.create(state())) return false;
    auto next = state("wf-state", 1);
    next.branch_decisions = std::nullopt;
    next.updated_at_utc = "2026-09-25T12:01:00Z";

    if (!store.update(next, 0)) return false;
    if (store.update(next, 0)) return false;

    const auto loaded = store.find_by_workflow_id("wf-state");
    return loaded.has_value() &&
           loaded->revision == 1 &&
           !loaded->branch_decisions.has_value() &&
           loaded->updated_at_utc == "2026-09-25T12:01:00Z";
}

[[nodiscard]] bool immutable_workflow_contract() {
    TempDatabase db{"immutable"};
    infrastructure::sqlite::SqliteConnection connection{db.path()};
    migrate(connection);
    infrastructure::sqlite::SqliteWorkflowStateStore store{connection};

    if (!store.create(state())) return false;
    auto modified = state("wf-state", 1, "Changed definition");
    modified.updated_at_utc = "2026-09-25T12:01:00Z";
    return !store.update(modified, 0);
}

[[nodiscard]] bool list_contract() {
    TempDatabase db{"list"};
    infrastructure::sqlite::SqliteConnection connection{db.path()};
    migrate(connection);
    infrastructure::sqlite::SqliteWorkflowStateStore store{connection};

    if (!store.create(state("wf-z"))) return false;
    if (!store.create(state("wf-a"))) return false;
    const auto values = store.list();
    return values.size() == 2U &&
           values[0].workflow.id().value() == "wf-a" &&
           values[1].workflow.id().value() == "wf-z";
}

[[nodiscard]] bool constraints_contract() {
    TempDatabase db{"constraints"};
    infrastructure::sqlite::SqliteConnection connection{db.path()};
    migrate(connection);
    infrastructure::sqlite::SqliteWorkflowStateStore store{connection};
    if (!store.create(state())) return false;

    bool branch_rejected = false;
    try {
        connection.execute(R"sql(
            INSERT INTO workflow_branch_decisions(
                workflow_id, node_id, ordinal, state, reason, condition_result
            ) VALUES (
                'wf-state', 'invalid-node', 99,
                'selected', 'condition_false', 0
            );
        )sql");
    } catch (...) {
        branch_rejected = true;
    }

    bool attempt_rejected = false;
    try {
        connection.execute(R"sql(
            UPDATE workflow_node_checkpoints
            SET state = 'pending'
            WHERE workflow_id = 'wf-state' AND node_id = 'source';
        )sql");
    } catch (...) {
        attempt_rejected = true;
    }

    return branch_rejected && attempt_rejected;
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    const std::string_view name{argv[1]};
    bool passed = false;
    if (name == "round-trip") passed = round_trip_contract();
    else if (name == "update") passed = update_contract();
    else if (name == "immutable") passed = immutable_workflow_contract();
    else if (name == "list") passed = list_contract();
    else if (name == "constraints") passed = constraints_contract();
    else return EXIT_FAILURE;

    if (!passed) {
        std::cerr << "SQLite workflow state store test failed: " << name << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "SQLite workflow state store test passed: " << name << '\n';
    return EXIT_SUCCESS;
}
