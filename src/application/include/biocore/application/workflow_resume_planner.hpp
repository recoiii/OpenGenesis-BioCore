#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_workflow_checkpoint_artifact_verifier.hpp"
#include "biocore/domain/workflow.hpp"
#include "biocore/domain/workflow_checkpoint.hpp"

namespace biocore::application {

struct WorkflowRetryLimitOverride final {
    domain::WorkflowNodeId node_id;
    std::uint32_t max_attempts{1U};
};

struct WorkflowRetryPolicy final {
    std::uint32_t default_max_attempts{3U};
    std::vector<WorkflowRetryLimitOverride> overrides;
};

enum class WorkflowResumeActionKind {
    reuse_completed,
    execute,
    retry,
    reevaluate_condition,
    preserve_skipped,
    preserve_blocked,
    exhausted,
};

[[nodiscard]] std::string_view to_string(WorkflowResumeActionKind kind) noexcept;

enum class WorkflowResumeReason {
    completed_verified,
    pending_execution,
    failed_retry,
    interrupted_retry,
    incomplete_retry,
    artifact_integrity_failed,
    upstream_recomputed,
    checkpoint_skipped,
    checkpoint_blocked,
    dependency_unavailable,
    attempt_limit_reached,
};

[[nodiscard]] std::string_view to_string(WorkflowResumeReason reason) noexcept;

struct WorkflowNodeResumeAction final {
    domain::WorkflowNodeId node_id;
    WorkflowResumeActionKind action{WorkflowResumeActionKind::execute};
    WorkflowResumeReason reason{WorkflowResumeReason::pending_execution};
    std::optional<std::uint32_t> next_attempt_number;
    std::optional<domain::WorkflowCheckpointFailure> previous_failure;
};

class WorkflowResumePlan final {
public:
    WorkflowResumePlan(
        domain::WorkflowId workflow_id,
        std::vector<WorkflowNodeResumeAction> actions
    );

    [[nodiscard]] const domain::WorkflowId& workflow_id() const noexcept;
    [[nodiscard]] const std::vector<WorkflowNodeResumeAction>& actions() const noexcept;

private:
    domain::WorkflowId workflow_id_;
    std::vector<WorkflowNodeResumeAction> actions_;
};

enum class WorkflowResumeErrorCode {
    invalid_retry_policy,
    duplicate_retry_override,
    unknown_retry_override_node,
    workflow_id_mismatch,
    missing_checkpoint_node,
    unknown_checkpoint_node,
};

[[nodiscard]] std::string_view to_string(WorkflowResumeErrorCode code) noexcept;

class WorkflowResumeError final : public std::invalid_argument {
public:
    WorkflowResumeError(WorkflowResumeErrorCode code, std::string message);

    [[nodiscard]] WorkflowResumeErrorCode code() const noexcept;

private:
    WorkflowResumeErrorCode code_;
};

[[nodiscard]] domain::WorkflowCheckpointManifest initialize_workflow_checkpoint_manifest(
    const domain::Workflow& workflow,
    const WorkflowRetryPolicy& policy
);

[[nodiscard]] WorkflowResumePlan plan_workflow_resume(
    const domain::Workflow& workflow,
    const domain::WorkflowCheckpointManifest& manifest,
    const IWorkflowCheckpointArtifactVerifier& verifier
);

}  // namespace biocore::application
