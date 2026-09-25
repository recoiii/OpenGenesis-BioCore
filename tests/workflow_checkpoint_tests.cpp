#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include "biocore/domain/workflow_checkpoint.hpp"

namespace {

using namespace biocore::domain;

[[nodiscard]] std::string sha() {
    return std::string(64U, 'a');
}

template <typename Function>
[[nodiscard]] bool rejects(Function&& function) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_contract() {
    const WorkflowCheckpointManifest manifest{
        WorkflowCheckpointManifest::current_schema_version,
        WorkflowId{"wf-checkpoint"},
        {
            {
                WorkflowNodeId{"a"},
                WorkflowCheckpointNodeState::completed,
                1U,
                3U,
                {{"result", "outputs/result.txt", 42, sha()}},
                std::nullopt,
            },
            {
                WorkflowNodeId{"b"},
                WorkflowCheckpointNodeState::failed,
                2U,
                3U,
                {},
                WorkflowCheckpointFailure{"worker failed", 2},
            },
            {
                WorkflowNodeId{"c"},
                WorkflowCheckpointNodeState::pending,
                0U,
                1U,
                {},
                std::nullopt,
            },
        },
    };

    return manifest.schema_version() == 1U &&
           manifest.workflow_id().value() == "wf-checkpoint" &&
           manifest.nodes().size() == 3U &&
           manifest.nodes()[0].outputs[0].sha256 == sha() &&
           manifest.nodes()[1].failure.has_value();
}

[[nodiscard]] bool invalid_contract() {
    return rejects([] {
               static_cast<void>(WorkflowCheckpointManifest{
                   2U, WorkflowId{"wf-x"}, {}
               });
           }) &&
           rejects([] {
               static_cast<void>(WorkflowCheckpointManifest{
                   1U,
                   WorkflowId{"wf-x"},
                   {{
                       WorkflowNodeId{"a"},
                       WorkflowCheckpointNodeState::pending,
                       1U,
                       3U,
                       {},
                       std::nullopt,
                   }}
               });
           }) &&
           rejects([] {
               static_cast<void>(WorkflowCheckpointManifest{
                   1U,
                   WorkflowId{"wf-x"},
                   {{
                       WorkflowNodeId{"a"},
                       WorkflowCheckpointNodeState::failed,
                       1U,
                       3U,
                       {},
                       std::nullopt,
                   }}
               });
           }) &&
           rejects([] {
               static_cast<void>(WorkflowCheckpointManifest{
                   1U,
                   WorkflowId{"wf-x"},
                   {{
                       WorkflowNodeId{"a"},
                       WorkflowCheckpointNodeState::completed,
                       1U,
                       3U,
                       {
                           {"result", "a.txt", 1, std::string(64U, 'a')},
                           {"result", "b.txt", 1, std::string(64U, 'b')},
                       },
                       std::nullopt,
                   }}
               });
           }) &&
           rejects([] {
               static_cast<void>(WorkflowCheckpointManifest{
                   1U,
                   WorkflowId{"wf-x"},
                   {{
                       WorkflowNodeId{"a"},
                       WorkflowCheckpointNodeState::completed,
                       1U,
                       3U,
                       {{"result", "a.txt", 1, "BAD"}},
                       std::nullopt,
                   }}
               });
           });
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    const std::string_view name{argv[1]};
    const bool passed =
        (name == "valid" && valid_contract()) ||
        (name == "invalid" && invalid_contract());
    if (!passed) {
        std::cerr << "Workflow checkpoint domain test failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Workflow checkpoint domain test passed\n";
    return EXIT_SUCCESS;
}
