#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/domain/workflow.hpp"

namespace biocore::domain {

enum class WorkflowCheckpointNodeState {
    pending,
    running,
    completed,
    failed,
    interrupted,
    skipped,
    blocked,
};

[[nodiscard]] std::string_view to_string(WorkflowCheckpointNodeState state) noexcept;

struct WorkflowCheckpointArtifact final {
    std::string output_port;
    std::string relative_project_path;
    std::int64_t size_bytes{0};
    std::string sha256;
};

struct WorkflowCheckpointFailure final {
    std::string message;
    std::optional<std::int64_t> exit_code;
};

struct WorkflowNodeCheckpoint final {
    WorkflowNodeId node_id;
    WorkflowCheckpointNodeState state{WorkflowCheckpointNodeState::pending};
    std::uint32_t attempt_number{0U};
    std::uint32_t max_attempts{1U};
    std::vector<WorkflowCheckpointArtifact> outputs;
    std::optional<WorkflowCheckpointFailure> failure;
};

class WorkflowCheckpointManifest final {
public:
    static constexpr std::uint32_t current_schema_version = 1U;
    static constexpr std::uint32_t maximum_attempts = 64U;
    static constexpr std::size_t maximum_failure_message_length = 16U * 1024U;
    static constexpr std::size_t maximum_path_length = 4096U;

    WorkflowCheckpointManifest(
        std::uint32_t schema_version,
        WorkflowId workflow_id,
        std::vector<WorkflowNodeCheckpoint> nodes
    );

    [[nodiscard]] std::uint32_t schema_version() const noexcept;
    [[nodiscard]] const WorkflowId& workflow_id() const noexcept;
    [[nodiscard]] const std::vector<WorkflowNodeCheckpoint>& nodes() const noexcept;

private:
    std::uint32_t schema_version_;
    WorkflowId workflow_id_;
    std::vector<WorkflowNodeCheckpoint> nodes_;
};

}  // namespace biocore::domain
