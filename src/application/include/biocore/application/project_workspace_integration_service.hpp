#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/project_workspace_integration.hpp"

namespace biocore::application {

class BatchExecutionService;
class BatchPlanningService;
class BatchRecoveryService;
class BatchResultsService;
class IBatchPlanStore;
class ICurrentProjectStore;
class IManagedFileRepository;
class IProjectResearchMetadataStore;
class IProjectSampleBindingStore;
class IProjectSampleStore;
class IUtcClock;
class SampleBindingService;
class SampleRegistryImportService;

class ProjectWorkspaceIntegrationService final {
public:
    static constexpr std::size_t maximum_cached_previews = 32U;

    ProjectWorkspaceIntegrationService(
        ICurrentProjectStore& current_project,
        IProjectResearchMetadataStore& research,
        IProjectSampleStore& samples,
        IProjectSampleBindingStore& bindings,
        IManagedFileRepository& managed_files,
        SampleRegistryImportService& sample_import,
        SampleBindingService& sample_binding,
        BatchPlanningService& batch_planning,
        IBatchPlanStore& batch_plans,
        BatchExecutionService& batch_execution,
        BatchRecoveryService& batch_recovery,
        BatchResultsService& batch_results,
        IUtcClock& clock
    ) noexcept;

    [[nodiscard]] ProjectWorkspaceSnapshot snapshot();

    [[nodiscard]] SampleImportPreview preview_sample_import(
        std::string_view table_text,
        SampleTableFormat format
    );
    [[nodiscard]] SampleImportPreview commit_sample_import(
        std::string_view table_text,
        SampleTableFormat format
    );

    [[nodiscard]] SampleBindingPreview preview_binding(
        const ProjectWorkspaceBindingRequest& request
    );
    [[nodiscard]] SampleBindingPreview commit_binding(
        const ProjectWorkspaceBindingRequest& request
    );

    [[nodiscard]] BatchPlanPreview preview_batch(
        const ProjectWorkspaceBatchPreviewRequest& request
    );
    [[nodiscard]] ApprovedBatchPlan approve_batch(
        std::string_view plan_id,
        const std::vector<std::string>& excluded_sample_ids
    );

    [[nodiscard]] BatchExecutionSnapshot submit_batch(
        std::string_view plan_id,
        const ProjectWorkspaceBatchSubmitRequest& request
    );
    [[nodiscard]] std::optional<BatchExecutionSnapshot> find_batch(
        std::string_view plan_id
    );
    [[nodiscard]] BatchExecutionSnapshot cancel_batch(std::string_view plan_id);

    [[nodiscard]] BatchRecoveryInspection inspect_recovery(std::string_view plan_id);
    [[nodiscard]] BatchExecutionAttemptRecord resume_sample(
        std::string_view plan_id,
        std::string_view sample_id,
        domain::JobPriority priority = domain::JobPriority::normal
    );
    [[nodiscard]] BatchExecutionAttemptRecord retry_sample(
        std::string_view plan_id,
        std::string_view sample_id,
        domain::JobPriority priority = domain::JobPriority::normal
    );

    [[nodiscard]] BatchResultsOverview results(std::string_view plan_id);

private:
    [[nodiscard]] domain::Project require_project();
    [[nodiscard]] domain::ProjectSampleBinding make_binding(
        const domain::Project& project,
        const ProjectWorkspaceBindingRequest& request
    ) const;
    void cache_preview(BatchPlanPreview preview);

    ICurrentProjectStore& current_project_;
    IProjectResearchMetadataStore& research_;
    IProjectSampleStore& samples_;
    IProjectSampleBindingStore& bindings_;
    IManagedFileRepository& managed_files_;
    SampleRegistryImportService& sample_import_;
    SampleBindingService& sample_binding_;
    BatchPlanningService& batch_planning_;
    IBatchPlanStore& batch_plans_;
    BatchExecutionService& batch_execution_;
    BatchRecoveryService& batch_recovery_;
    BatchResultsService& batch_results_;
    IUtcClock& clock_;
    std::map<std::string, BatchPlanPreview, std::less<>> preview_cache_;
};

}  // namespace biocore::application
