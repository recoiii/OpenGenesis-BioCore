#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/project_workspace_integration.hpp"

namespace biocore::presentation {

[[nodiscard]] std::string render_project_workspace_snapshot(
    const application::ProjectWorkspaceSnapshot& snapshot
);
[[nodiscard]] std::string render_sample_import_preview(
    const application::SampleImportPreview& preview
);
[[nodiscard]] std::string render_sample_binding_preview(
    const application::SampleBindingPreview& preview
);
[[nodiscard]] std::string render_batch_plan_preview(
    const application::BatchPlanPreview& preview
);
[[nodiscard]] std::string render_approved_batch_plan(
    const application::ApprovedBatchPlan& plan
);
[[nodiscard]] std::string render_batch_execution_snapshot(
    const application::BatchExecutionSnapshot& snapshot
);
[[nodiscard]] std::string render_batch_recovery_inspection(
    const application::BatchRecoveryInspection& inspection
);
[[nodiscard]] std::string render_batch_results_overview(
    const application::BatchResultsOverview& overview
);
[[nodiscard]] std::string render_batch_attempt(
    const application::BatchExecutionAttemptRecord& attempt
);

[[nodiscard]] application::ProjectWorkspaceBindingRequest parse_workspace_binding_request(
    std::string_view sample_id,
    std::string_view body
);
[[nodiscard]] application::ProjectWorkspaceBatchPreviewRequest parse_workspace_batch_preview_request(
    std::string_view body
);
[[nodiscard]] std::vector<std::string> parse_workspace_exclusions(std::string_view body);
[[nodiscard]] application::ProjectWorkspaceBatchSubmitRequest parse_workspace_batch_submit_request(
    std::string_view body
);
[[nodiscard]] domain::JobPriority parse_workspace_recovery_priority(std::string_view body);

}  // namespace biocore::presentation
