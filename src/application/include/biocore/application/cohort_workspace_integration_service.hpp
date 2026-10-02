#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "biocore/application/cohort_analysis_snapshot.hpp"
#include "biocore/application/cohort_execution.hpp"
#include "biocore/application/cohort_execution_service.hpp"
#include "biocore/application/cohort_registry.hpp"
#include "biocore/application/cohort_registry_service.hpp"

namespace biocore::application {

class CohortAnalysisApprovalService;
class CohortAnalysisSelectionService;
class ICurrentProjectStore;

class CohortWorkspaceIntegrationService final {
public:
    CohortWorkspaceIntegrationService(
        ICurrentProjectStore& current_project,
        CohortRegistryService& cohorts,
        CohortAnalysisSelectionService& selections,
        CohortAnalysisApprovalService& approvals,
        CohortExecutionService& execution
    ) noexcept;

    [[nodiscard]] std::vector<CohortDefinition> list_cohorts();
    [[nodiscard]] CohortDefinition create_cohort(
        std::string name,
        std::vector<CohortMemberDraft> members
    );
    [[nodiscard]] CohortDefinition revise_cohort(
        std::string_view cohort_id,
        std::uint32_t expected_revision,
        std::vector<CohortMemberDraft> members
    );
    [[nodiscard]] std::optional<CohortDefinition> find_cohort(
        std::string_view cohort_id,
        std::optional<std::uint32_t> revision = std::nullopt
    );

    [[nodiscard]] CohortAnalysisSelectionPreview preview_selection(
        CohortAnalysisSelectionRequest request
    );
    [[nodiscard]] CohortAnalysisApprovalPreview preview_analysis(
        CohortAnalysisApprovalRequest request
    );
    [[nodiscard]] CohortAnalysisSnapshot approve_analysis(
        ApproveCohortAnalysisRequest request
    );

    [[nodiscard]] CohortExecutionAttempt submit_analysis(
        SubmitCohortExecutionRequest request
    );
    [[nodiscard]] CohortExecutionHistory analysis_history(
        std::string_view analysis_id
    );
    [[nodiscard]] CohortExecutionAttempt cancel_analysis(
        std::string_view analysis_id,
        std::string_view attempt_id
    );
    [[nodiscard]] CohortExecutionAttempt retry_analysis(
        RetryCohortExecutionRequest request
    );

    [[nodiscard]] std::string current_project_id();

private:
    [[nodiscard]] std::string require_project_id();

    ICurrentProjectStore& current_project_;
    CohortRegistryService& cohorts_;
    CohortAnalysisSelectionService& selections_;
    CohortAnalysisApprovalService& approvals_;
    CohortExecutionService& execution_;
};

}  // namespace biocore::application
