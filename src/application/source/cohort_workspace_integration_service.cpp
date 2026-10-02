#include "biocore/application/cohort_workspace_integration_service.hpp"

#include <stdexcept>
#include <utility>

#include "biocore/application/cohort_analysis_approval_service.hpp"
#include "biocore/application/cohort_analysis_selection_service.hpp"
#include "biocore/application/i_current_project_store.hpp"

namespace biocore::application {

CohortWorkspaceIntegrationService::CohortWorkspaceIntegrationService(
    ICurrentProjectStore& current_project,
    CohortRegistryService& cohorts,
    CohortAnalysisSelectionService& selections,
    CohortAnalysisApprovalService& approvals,
    CohortExecutionService& execution
) noexcept
    : current_project_{current_project},
      cohorts_{cohorts},
      selections_{selections},
      approvals_{approvals},
      execution_{execution} {}

std::string CohortWorkspaceIntegrationService::require_project_id() {
    const auto project = current_project_.find();
    if (!project.has_value()) {
        throw std::runtime_error{"Current project metadata is unavailable"};
    }
    return std::string{project->id()};
}

std::string CohortWorkspaceIntegrationService::current_project_id() {
    return require_project_id();
}

std::vector<CohortDefinition> CohortWorkspaceIntegrationService::list_cohorts() {
    return cohorts_.list(require_project_id());
}

CohortDefinition CohortWorkspaceIntegrationService::create_cohort(
    std::string name,
    std::vector<CohortMemberDraft> members
) {
    return cohorts_.create({
        .project_id = require_project_id(),
        .name = std::move(name),
        .members = std::move(members),
    });
}

CohortDefinition CohortWorkspaceIntegrationService::revise_cohort(
    const std::string_view cohort_id,
    const std::uint32_t expected_revision,
    std::vector<CohortMemberDraft> members
) {
    return cohorts_.revise({
        .project_id = require_project_id(),
        .cohort_id = std::string{cohort_id},
        .expected_revision = expected_revision,
        .members = std::move(members),
    });
}

std::optional<CohortDefinition> CohortWorkspaceIntegrationService::find_cohort(
    const std::string_view cohort_id,
    const std::optional<std::uint32_t> revision
) {
    return cohorts_.find(require_project_id(), cohort_id, revision);
}

CohortAnalysisSelectionPreview CohortWorkspaceIntegrationService::preview_selection(
    CohortAnalysisSelectionRequest request
) {
    request.project_id = require_project_id();
    return selections_.preview(request);
}

CohortAnalysisApprovalPreview CohortWorkspaceIntegrationService::preview_analysis(
    CohortAnalysisApprovalRequest request
) {
    request.selection.project_id = require_project_id();
    return approvals_.preview(request);
}

CohortAnalysisSnapshot CohortWorkspaceIntegrationService::approve_analysis(
    ApproveCohortAnalysisRequest request
) {
    request.preview_request.selection.project_id = require_project_id();
    return approvals_.approve(request);
}

CohortExecutionAttempt CohortWorkspaceIntegrationService::submit_analysis(
    SubmitCohortExecutionRequest request
) {
    request.project_id = require_project_id();
    return execution_.submit(request);
}

CohortExecutionHistory CohortWorkspaceIntegrationService::analysis_history(
    const std::string_view analysis_id
) {
    return execution_.reconcile(require_project_id(), analysis_id);
}

CohortExecutionAttempt CohortWorkspaceIntegrationService::cancel_analysis(
    const std::string_view analysis_id,
    const std::string_view attempt_id
) {
    return execution_.cancel(require_project_id(), analysis_id, attempt_id);
}

CohortExecutionAttempt CohortWorkspaceIntegrationService::retry_analysis(
    RetryCohortExecutionRequest request
) {
    request.project_id = require_project_id();
    return execution_.retry(request);
}

}  // namespace biocore::application
