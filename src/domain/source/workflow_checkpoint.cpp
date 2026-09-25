#include "biocore/domain/workflow_checkpoint.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#include <utility>

namespace biocore::domain {
namespace {

[[nodiscard]] bool valid_text(
    const std::string_view value,
    const std::size_t maximum_length
) noexcept {
    return !value.empty() && value.size() <= maximum_length &&
           value.find('\0') == std::string_view::npos &&
           std::ranges::none_of(value, [](const char character) {
               return std::iscntrl(static_cast<unsigned char>(character)) != 0;
           });
}

[[nodiscard]] bool valid_port_name(const std::string_view value) noexcept {
    if (value.empty() || value.size() > 64U ||
        !(value.front() >= 'a' && value.front() <= 'z')) {
        return false;
    }
    return std::ranges::all_of(value, [](const char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') ||
               character == '-' || character == '_';
    });
}

[[nodiscard]] bool valid_sha256(const std::string_view value) noexcept {
    return value.size() == 64U &&
           std::ranges::all_of(value, [](const char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

}  // namespace

std::string_view to_string(const WorkflowCheckpointNodeState state) noexcept {
    switch (state) {
        case WorkflowCheckpointNodeState::pending: return "pending";
        case WorkflowCheckpointNodeState::running: return "running";
        case WorkflowCheckpointNodeState::completed: return "completed";
        case WorkflowCheckpointNodeState::failed: return "failed";
        case WorkflowCheckpointNodeState::interrupted: return "interrupted";
        case WorkflowCheckpointNodeState::skipped: return "skipped";
        case WorkflowCheckpointNodeState::blocked: return "blocked";
    }
    return "unknown";
}

std::optional<WorkflowCheckpointNodeState> workflow_checkpoint_node_state_from_string(
    const std::string_view value
) noexcept {
    if (value == "pending") return WorkflowCheckpointNodeState::pending;
    if (value == "running") return WorkflowCheckpointNodeState::running;
    if (value == "completed") return WorkflowCheckpointNodeState::completed;
    if (value == "failed") return WorkflowCheckpointNodeState::failed;
    if (value == "interrupted") return WorkflowCheckpointNodeState::interrupted;
    if (value == "skipped") return WorkflowCheckpointNodeState::skipped;
    if (value == "blocked") return WorkflowCheckpointNodeState::blocked;
    return std::nullopt;
}

WorkflowCheckpointManifest::WorkflowCheckpointManifest(
    const std::uint32_t schema_version,
    WorkflowId workflow_id,
    std::vector<WorkflowNodeCheckpoint> nodes
)
    : schema_version_{schema_version},
      workflow_id_{std::move(workflow_id)},
      nodes_{std::move(nodes)} {
    if (schema_version_ != current_schema_version) {
        throw std::invalid_argument("Workflow checkpoint schema version is unsupported");
    }

    std::set<std::string, std::less<>> node_ids;
    for (const WorkflowNodeCheckpoint& node : nodes_) {
        if (!node_ids.emplace(node.node_id.value()).second) {
            throw std::invalid_argument(
                "Workflow checkpoint manifest contains duplicate node identifiers"
            );
        }
        if (node.max_attempts == 0U || node.max_attempts > maximum_attempts ||
            node.attempt_number > node.max_attempts) {
            throw std::invalid_argument("Workflow checkpoint attempt limits are invalid");
        }

        const bool never_started =
            node.state == WorkflowCheckpointNodeState::pending ||
            node.state == WorkflowCheckpointNodeState::skipped ||
            node.state == WorkflowCheckpointNodeState::blocked;
        if (never_started && node.attempt_number != 0U) {
            throw std::invalid_argument(
                "Pending/skipped/blocked checkpoints must not consume an attempt"
            );
        }
        if (!never_started && node.attempt_number == 0U) {
            throw std::invalid_argument(
                "Started workflow checkpoints require a positive attempt number"
            );
        }

        const bool failure_state =
            node.state == WorkflowCheckpointNodeState::failed ||
            node.state == WorkflowCheckpointNodeState::interrupted;
        if (failure_state != node.failure.has_value()) {
            throw std::invalid_argument(
                "Workflow checkpoint failure evidence is inconsistent with node state"
            );
        }
        if (node.failure.has_value() &&
            !valid_text(node.failure->message, maximum_failure_message_length)) {
            throw std::invalid_argument("Workflow checkpoint failure message is invalid");
        }

        std::set<std::string, std::less<>> ports;
        for (const WorkflowCheckpointArtifact& artifact : node.outputs) {
            if (!valid_port_name(artifact.output_port) ||
                !ports.emplace(artifact.output_port).second ||
                !valid_text(artifact.relative_project_path, maximum_path_length) ||
                artifact.size_bytes < 0 ||
                !valid_sha256(artifact.sha256)) {
                throw std::invalid_argument(
                    "Workflow checkpoint output artifact is invalid"
                );
            }
        }
    }
}

std::uint32_t WorkflowCheckpointManifest::schema_version() const noexcept {
    return schema_version_;
}

const WorkflowId& WorkflowCheckpointManifest::workflow_id() const noexcept {
    return workflow_id_;
}

const std::vector<WorkflowNodeCheckpoint>&
WorkflowCheckpointManifest::nodes() const noexcept {
    return nodes_;
}

}  // namespace biocore::domain
