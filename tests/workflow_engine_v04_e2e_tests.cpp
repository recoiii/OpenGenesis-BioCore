#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/workflow_execution_workspace_service.hpp"
#include "biocore/application/workflow_state_recovery_service.hpp"
#include "biocore/application/workflow_state_service.hpp"
#include "biocore/domain/workflow_checkpoint.hpp"
#include "biocore/infrastructure/filesystem_workflow_checkpoint_artifact_verifier.hpp"
#include "biocore/infrastructure/sqlite/project_migration_runner.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_workflow_state_store.hpp"
#include "biocore/pipeline_protocol/workflow_document_codec.hpp"

namespace {

namespace fs = std::filesystem;
using namespace biocore;

class TempProject final {
public:
    TempProject() {
        root_ = fs::temp_directory_path() /
            ("biocore-v04-e2e-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            ));
        fs::create_directories(root_ / ".biocore");
        fs::create_directories(root_ / "outputs");
        root_ = fs::canonical(root_);
    }

    ~TempProject() {
        std::error_code ignored;
        fs::remove_all(root_, ignored);
    }

    [[nodiscard]] const fs::path& root() const noexcept { return root_; }
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
        return "2026-09-25T17:00:" +
               (tick_ < 10 ? std::string{"0"} : std::string{}) +
               std::to_string(tick_) + "Z";
    }

private:
    int tick_{0};
};

[[nodiscard]] bool run_e2e() {
    TempProject project;
    Clock clock;

    const std::string document = R"({
        "schemaVersion":1,
        "id":"org.biocore.workflow.release-e2e",
        "name":"Release E2E",
        "description":"v0.4 closure workflow",
        "nodes":[
            {
                "id":"source",
                "label":"Source",
                "moduleId":"org.biocore.test.source",
                "pluginVersion":"1.0.0",
                "inputs":[],
                "outputs":[{"name":"result","artifactType":"txt"}],
                "parameters":{}
            },
            {
                "id":"target",
                "label":"Target",
                "moduleId":"org.biocore.test.target",
                "pluginVersion":"1.0.0",
                "inputs":[{"name":"input","artifactType":"txt","required":true}],
                "outputs":[{"name":"report","artifactType":"json"}],
                "parameters":{}
            }
        ],
        "edges":[
            {
                "sourceNode":"source",
                "sourceOutput":"result",
                "targetNode":"target",
                "targetInput":"input"
            }
        ]
    })";
    const domain::Workflow workflow =
        pipeline_protocol::parse_workflow_document(document);

    {
        std::ofstream output{project.root() / "outputs" / "source.txt", std::ios::binary};
        output << "abc";
    }

    {
        infrastructure::sqlite::SqliteConnection connection{project.database()};
        infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
        migrations.apply_pending();

        infrastructure::sqlite::SqliteWorkflowStateStore store{connection};
        application::WorkflowStateService states{store, clock};
        infrastructure::FilesystemWorkflowCheckpointArtifactVerifier verifier{
            project.root()
        };
        application::WorkflowExecutionWorkspaceService workspace{
            states, verifier
        };

        const auto created = workspace.create(workflow);
        if (created.revision != 0 || created.nodes.size() != 2U) return false;

        domain::WorkflowCheckpointManifest checkpoint{
            1U,
            domain::WorkflowId{"org.biocore.workflow.release-e2e"},
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
                    0U,
                    3U,
                    {},
                    std::nullopt
                },
            }
        };
        const auto updated = states.update(
            created.workflow_id,
            std::move(checkpoint),
            std::nullopt,
            created.revision
        );
        if (updated.revision != 1) return false;
    }

    {
        infrastructure::sqlite::SqliteConnection connection{project.database()};
        infrastructure::sqlite::ProjectMigrationRunner migrations{connection};
        migrations.apply_pending();

        infrastructure::sqlite::SqliteWorkflowStateStore store{connection};
        infrastructure::FilesystemWorkflowCheckpointArtifactVerifier verifier{
            project.root()
        };
        application::WorkflowStateRecoveryService recovery{
            store, verifier, clock
        };
        const auto recovered = recovery.recover();
        if (recovered.recovered.size() != 1U || !recovered.issues.empty()) {
            return false;
        }

        application::WorkflowStateService states{store, clock};
        application::WorkflowExecutionWorkspaceService workspace{
            states, verifier
        };
        const auto snapshot = workspace.find(
            "org.biocore.workflow.release-e2e"
        );
        if (!snapshot.has_value() ||
            snapshot->revision != 1 ||
            snapshot->nodes.size() != 2U) {
            return false;
        }

        return snapshot->nodes[0].resume_action ==
                   application::WorkflowResumeActionKind::reuse_completed &&
               snapshot->nodes[0].resume_reason ==
                   application::WorkflowResumeReason::completed_verified &&
               snapshot->nodes[1].resume_action ==
                   application::WorkflowResumeActionKind::execute &&
               snapshot->nodes[1].next_attempt_number ==
                   std::optional<std::uint32_t>{1U};
    }
}

}  // namespace

int main() {
    return run_e2e() ? EXIT_SUCCESS : EXIT_FAILURE;
}
