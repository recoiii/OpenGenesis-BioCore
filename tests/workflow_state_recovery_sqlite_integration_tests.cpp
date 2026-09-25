#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/workflow_state_recovery_service.hpp"
#include "biocore/application/workflow_state_service.hpp"
#include "biocore/domain/workflow.hpp"
#include "biocore/domain/workflow_checkpoint.hpp"
#include "biocore/infrastructure/filesystem_workflow_checkpoint_artifact_verifier.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_workflow_state_store.hpp"

namespace {

using namespace biocore;
namespace fs = std::filesystem;

class TempProject final {
public:
    explicit TempProject(const std::string_view suffix) {
        root_ = fs::temp_directory_path() /
            ("biocore-workflow-recovery-" + std::string{suffix} + "-" +
             std::to_string(
                 std::chrono::steady_clock::now().time_since_epoch().count()
             ));
        fs::create_directories(root_ / ".biocore");
        fs::create_directories(root_ / "outputs");
        root_ = fs::canonical(root_);
    }

    ~TempProject() {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    [[nodiscard]] const fs::path& root() const noexcept {
        return root_;
    }

    [[nodiscard]] fs::path database() const {
        return root_ / ".biocore" / "project.sqlite";
    }

private:
    fs::path root_;
};

class Clock final : public application::IUtcClock {
public:
    std::string now_utc_iso8601() override {
        ++tick_;
        return "2026-09-25T13:00:" + (tick_ < 10 ? std::string{"0"} : std::string{}) +
               std::to_string(tick_) + "Z";
    }

private:
    int tick_{0};
};

[[nodiscard]] domain::Workflow workflow() {
    return domain::Workflow{
        domain::Workflow::current_schema_version,
        domain::WorkflowId{"wf-recovery"},
        "Recovery",
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

void migrate(infrastructure::sqlite::SqliteConnection& connection) {
    infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
    migrations.apply_pending();
    if (migrations.current_version() !=
        infrastructure::sqlite::latest_project_schema_version) {
        throw std::runtime_error("Current project schema required");
    }
}

[[nodiscard]] const application::WorkflowNodeResumeAction* action(
    const application::WorkflowResumePlan& plan,
    const std::string_view node_id
) {
    for (const auto& value : plan.actions()) {
        if (value.node_id.value() == node_id) return &value;
    }
    return nullptr;
}

[[nodiscard]] bool reopen_contract() {
    TempProject project{"reopen"};
    Clock clock;

    {
        infrastructure::sqlite::SqliteConnection connection{project.database()};
        migrate(connection);
        infrastructure::sqlite::SqliteWorkflowStateStore store{connection};
        application::WorkflowStateService states{store, clock};
        const auto created = states.create(
            workflow(), application::WorkflowRetryPolicy{3U, {}}
        );

        domain::WorkflowCheckpointManifest running{
            domain::WorkflowCheckpointManifest::current_schema_version,
            domain::WorkflowId{"wf-recovery"},
            {
                {
                    domain::WorkflowNodeId{"source"},
                    domain::WorkflowCheckpointNodeState::running,
                    1U, 3U, {}, std::nullopt
                },
                {
                    domain::WorkflowNodeId{"target"},
                    domain::WorkflowCheckpointNodeState::pending,
                    0U, 3U, {}, std::nullopt
                },
            }
        };
        const auto updated = states.update(
            "wf-recovery", std::move(running), std::nullopt, created.revision
        );
        if (updated.revision != 1) return false;
    }

    infrastructure::sqlite::SqliteConnection reopened{project.database()};
    migrate(reopened);
    infrastructure::sqlite::SqliteWorkflowStateStore store{reopened};
    infrastructure::FilesystemWorkflowCheckpointArtifactVerifier verifier{
        project.root()
    };
    application::WorkflowStateRecoveryService recovery{store, verifier, clock};

    const auto first = recovery.recover();
    const auto persisted = store.find_by_workflow_id("wf-recovery");
    if (first.recovered.size() != 1U || !first.issues.empty() ||
        !persisted.has_value() || persisted->revision != 2 ||
        persisted->checkpoint.nodes()[0].state !=
            domain::WorkflowCheckpointNodeState::interrupted ||
        !persisted->checkpoint.nodes()[0].failure.has_value()) {
        return false;
    }

    const auto* source = action(first.recovered[0].resume_plan, "source");
    if (source == nullptr ||
        source->action != application::WorkflowResumeActionKind::retry ||
        source->next_attempt_number != std::optional<std::uint32_t>{2U}) {
        return false;
    }

    const auto second = recovery.recover();
    const auto persisted_again = store.find_by_workflow_id("wf-recovery");
    return second.recovered.size() == 1U && second.issues.empty() &&
           persisted_again.has_value() && persisted_again->revision == 2 &&
           !second.recovered[0].reconciled_running_checkpoint;
}

[[nodiscard]] bool verified_completed_contract() {
    TempProject project{"completed"};
    Clock clock;
    {
        std::ofstream stream{
            project.root() / "outputs" / "source.txt",
            std::ios::binary
        };
        stream << "abc";
    }

    {
        infrastructure::sqlite::SqliteConnection connection{project.database()};
        migrate(connection);
        infrastructure::sqlite::SqliteWorkflowStateStore store{connection};
        application::WorkflowStateService states{store, clock};
        const auto created = states.create(
            workflow(), application::WorkflowRetryPolicy{3U, {}}
        );

        domain::WorkflowCheckpointManifest completed{
            domain::WorkflowCheckpointManifest::current_schema_version,
            domain::WorkflowId{"wf-recovery"},
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
                            "ba7816bf8f01cfea414140de5dae2223"
                            "b00361a396177a9cb410ff61f20015ad"
                        }
                    },
                    std::nullopt
                },
                {
                    domain::WorkflowNodeId{"target"},
                    domain::WorkflowCheckpointNodeState::pending,
                    0U, 3U, {}, std::nullopt
                },
            }
        };
        static_cast<void>(states.update(
            "wf-recovery",
            std::move(completed),
            std::nullopt,
            created.revision
        ));
    }

    infrastructure::sqlite::SqliteConnection reopened{project.database()};
    migrate(reopened);
    infrastructure::sqlite::SqliteWorkflowStateStore store{reopened};
    infrastructure::FilesystemWorkflowCheckpointArtifactVerifier verifier{
        project.root()
    };
    application::WorkflowStateRecoveryService recovery{store, verifier, clock};
    const auto result = recovery.recover();
    if (result.recovered.size() != 1U || !result.issues.empty()) return false;

    const auto* source = action(result.recovered[0].resume_plan, "source");
    const auto* target = action(result.recovered[0].resume_plan, "target");
    return source != nullptr && target != nullptr &&
           source->action ==
               application::WorkflowResumeActionKind::reuse_completed &&
           target->action ==
               application::WorkflowResumeActionKind::execute;
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    const std::string_view name{argv[1]};
    bool passed = false;
    if (name == "reopen") passed = reopen_contract();
    else if (name == "verified-completed") passed = verified_completed_contract();
    else return EXIT_FAILURE;

    if (!passed) {
        std::cerr << "Workflow state SQLite recovery integration failed: "
                  << name << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Workflow state SQLite recovery integration passed: "
              << name << '\n';
    return EXIT_SUCCESS;
}
