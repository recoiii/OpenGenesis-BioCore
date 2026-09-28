#include "biocore/application/project_workspace_integration_service.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

#include "biocore/application/batch_execution_service.hpp"
#include "biocore/application/batch_planning_service.hpp"
#include "biocore/application/batch_recovery_service.hpp"
#include "biocore/application/batch_results_service.hpp"
#include "biocore/application/i_batch_plan_store.hpp"
#include "biocore/application/i_current_project_store.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_project_research_metadata_store.hpp"
#include "biocore/application/i_project_sample_binding_store.hpp"
#include "biocore/application/i_project_sample_store.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/sample_binding_service.hpp"
#include "biocore/application/sample_registry_import.hpp"

namespace biocore::application {
namespace {

[[nodiscard]] bool is_input_file(const domain::ManagedFile& file) noexcept {
    return file.storage_mode() != domain::StorageMode::generated_output &&
           file.storage_mode() != domain::StorageMode::temporary;
}

void add_bound_file_ids(
    const domain::ProjectSampleBinding& binding,
    std::set<std::string, std::less<>>& ids
) {
    ids.emplace(binding.primary_file_id());
    if (binding.secondary_file_id().has_value()) ids.emplace(*binding.secondary_file_id());
    if (binding.reference_file_id().has_value()) ids.emplace(*binding.reference_file_id());
}

[[nodiscard]] std::vector<ProjectWorkspaceLinkIssue> binding_issues(
    const domain::ProjectSampleBinding& binding,
    IManagedFileRepository& files
) {
    std::vector<ProjectWorkspaceLinkIssue> issues;
    const auto inspect = [&](const std::string_view file_id, const std::string_view role) {
        if (!files.find_by_id(file_id).has_value()) {
            issues.push_back(ProjectWorkspaceLinkIssue{
                .code = "missing_" + std::string{role} + "_file",
                .message = "Binding references a " + std::string{role} +
                           " managed file that no longer exists",
            });
        }
    };
    inspect(binding.primary_file_id(), "primary");
    if (binding.secondary_file_id().has_value()) inspect(*binding.secondary_file_id(), "secondary");
    if (binding.reference_file_id().has_value()) inspect(*binding.reference_file_id(), "reference");
    return issues;
}

}  // namespace

ProjectWorkspaceIntegrationService::ProjectWorkspaceIntegrationService(
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
) noexcept
    : current_project_{current_project}, research_{research}, samples_{samples}, bindings_{bindings},
      managed_files_{managed_files}, sample_import_{sample_import}, sample_binding_{sample_binding},
      batch_planning_{batch_planning}, batch_plans_{batch_plans}, batch_execution_{batch_execution},
      batch_recovery_{batch_recovery}, batch_results_{batch_results}, clock_{clock} {}

domain::Project ProjectWorkspaceIntegrationService::require_project() {
    const auto project = current_project_.find();
    if (!project.has_value()) {
        throw std::runtime_error{"Current project metadata is unavailable"};
    }
    return *project;
}

ProjectWorkspaceSnapshot ProjectWorkspaceIntegrationService::snapshot() {
    domain::Project project = require_project();
    ProjectWorkspaceSnapshot result{
        .project = project,
        .research = research_.find(project.id()),
        .samples = {},
        .files = {},
        .batches = {},
    };

    const auto samples = samples_.list(project.id());
    if (!samples.has_value()) {
        throw std::runtime_error{"Current project sample registry is unavailable"};
    }
    auto bindings = bindings_.list(project.id());
    std::ranges::sort(bindings, [](const auto& left, const auto& right) {
        return left.sample_id() < right.sample_id();
    });

    std::set<std::string, std::less<>> bound_file_ids;
    for (const auto& binding : bindings) add_bound_file_ids(binding, bound_file_ids);

    for (const auto& sample : *samples) {
        const auto found = std::ranges::find_if(bindings, [&](const auto& binding) {
            return binding.sample_id() == sample.sample_id();
        });
        ProjectWorkspaceSample view{
            .sample = sample,
            .binding = std::nullopt,
            .binding_complete = false,
            .issues = {},
        };
        if (found != bindings.end()) {
            view.binding = *found;
            view.issues = binding_issues(*found, managed_files_);
            view.binding_complete = view.issues.empty();
        } else {
            view.issues.push_back(ProjectWorkspaceLinkIssue{
                .code = "binding_missing",
                .message = "Sample does not yet have an input binding",
            });
        }
        result.samples.push_back(std::move(view));
    }
    std::ranges::sort(result.samples, [](const auto& left, const auto& right) {
        return left.sample.sample_id() < right.sample.sample_id();
    });

    for (const auto& file : managed_files_.list()) {
        if (!is_input_file(file)) continue;
        result.files.push_back(ProjectWorkspaceFile{
            .file_id = std::string{file.id()},
            .display_name = std::string{file.display_name()},
            .file_type = std::string{file.file_type()},
            .storage_mode = file.storage_mode(),
            .size_bytes = file.size_bytes(),
            .orphaned = !bound_file_ids.contains(file.id()),
        });
    }
    std::ranges::sort(result.files, [](const auto& left, const auto& right) {
        if (left.display_name != right.display_name) return left.display_name < right.display_name;
        return left.file_id < right.file_id;
    });

    for (const auto& plan : batch_plans_.list()) {
        if (plan.project_id != project.id()) continue;
        ProjectWorkspaceBatch batch{
            .plan_id = plan.plan_id,
            .template_id = plan.template_id,
            .template_version = plan.template_version,
            .approved_at_utc = plan.approved_at_utc,
            .included_samples = 0U,
            .excluded_samples = 0U,
            .execution = batch_execution_.find(plan.plan_id),
            .recovery_attention = 0U,
        };
        for (const auto& sample : plan.samples) {
            if (sample.disposition == BatchPlanSampleDisposition::included) ++batch.included_samples;
            else ++batch.excluded_samples;
        }
        if (batch.execution.has_value()) {
            const auto recovery = batch_recovery_.inspect(plan.plan_id);
            for (const auto& sample : recovery.samples) {
                if (sample.action != BatchRecoveryAction::none) ++batch.recovery_attention;
            }
        }
        result.batches.push_back(std::move(batch));
    }
    std::ranges::sort(result.batches, [](const auto& left, const auto& right) {
        if (left.approved_at_utc != right.approved_at_utc) return left.approved_at_utc < right.approved_at_utc;
        return left.plan_id < right.plan_id;
    });
    return result;
}

SampleImportPreview ProjectWorkspaceIntegrationService::preview_sample_import(
    const std::string_view table_text,
    const SampleTableFormat format
) {
    const auto project = require_project();
    return sample_import_.preview(project.id(), table_text, format);
}

SampleImportPreview ProjectWorkspaceIntegrationService::commit_sample_import(
    const std::string_view table_text,
    const SampleTableFormat format
) {
    auto preview = preview_sample_import(table_text, format);
    if (!preview.valid()) {
        throw std::invalid_argument{"Sample import preview contains blocking issues"};
    }
    sample_import_.commit(preview);
    return preview;
}

domain::ProjectSampleBinding ProjectWorkspaceIntegrationService::make_binding(
    const domain::Project& project,
    const ProjectWorkspaceBindingRequest& request
) const {
    return domain::ProjectSampleBinding{
        std::string{project.id()}, request.sample_id, request.layout,
        request.primary_file_id, request.secondary_file_id, request.reference_file_id
    };
}

SampleBindingPreview ProjectWorkspaceIntegrationService::preview_binding(
    const ProjectWorkspaceBindingRequest& request
) {
    const auto project = require_project();
    return sample_binding_.preview(make_binding(project, request));
}

SampleBindingPreview ProjectWorkspaceIntegrationService::commit_binding(
    const ProjectWorkspaceBindingRequest& request
) {
    const auto project = require_project();
    return sample_binding_.commit(make_binding(project, request));
}

void ProjectWorkspaceIntegrationService::cache_preview(BatchPlanPreview preview) {
    if (!preview_cache_.contains(preview.plan_id) &&
        preview_cache_.size() >= maximum_cached_previews) {
        preview_cache_.erase(preview_cache_.begin());
    }
    preview_cache_.insert_or_assign(preview.plan_id, std::move(preview));
}

BatchPlanPreview ProjectWorkspaceIntegrationService::preview_batch(
    const ProjectWorkspaceBatchPreviewRequest& request
) {
    const auto project = require_project();
    BatchPlanPreview preview = batch_planning_.preview(BatchPlanRequest{
        .plan_id = request.plan_id,
        .project_id = std::string{project.id()},
        .template_id = request.template_id,
        .template_version = request.template_version,
        .sample_ids = request.sample_ids,
        .parameter_overrides = request.parameter_overrides,
        .input_assignments = request.input_assignments,
    });
    cache_preview(preview);
    return preview;
}

ApprovedBatchPlan ProjectWorkspaceIntegrationService::approve_batch(
    const std::string_view plan_id,
    const std::vector<std::string>& excluded_sample_ids
) {
    const auto found = preview_cache_.find(plan_id);
    if (found == preview_cache_.end()) {
        throw std::invalid_argument{"Batch approval requires a current server-side preview"};
    }
    const auto approved = batch_planning_.approve(
        found->second, excluded_sample_ids, clock_.now_utc_iso8601()
    );
    preview_cache_.erase(found);
    return approved;
}

BatchExecutionSnapshot ProjectWorkspaceIntegrationService::submit_batch(
    const std::string_view plan_id,
    const ProjectWorkspaceBatchSubmitRequest& request
) {
    return batch_execution_.submit(plan_id, request.maximum_concurrent_jobs, request.priority);
}

std::optional<BatchExecutionSnapshot> ProjectWorkspaceIntegrationService::find_batch(
    const std::string_view plan_id
) {
    return batch_execution_.find(plan_id);
}

BatchExecutionSnapshot ProjectWorkspaceIntegrationService::cancel_batch(const std::string_view plan_id) {
    return batch_execution_.cancel(plan_id);
}

BatchRecoveryInspection ProjectWorkspaceIntegrationService::inspect_recovery(const std::string_view plan_id) {
    return batch_recovery_.inspect(plan_id);
}

BatchExecutionAttemptRecord ProjectWorkspaceIntegrationService::resume_sample(
    const std::string_view plan_id,
    const std::string_view sample_id,
    const domain::JobPriority priority
) {
    return batch_recovery_.resume(plan_id, sample_id, priority);
}

BatchExecutionAttemptRecord ProjectWorkspaceIntegrationService::retry_sample(
    const std::string_view plan_id,
    const std::string_view sample_id,
    const domain::JobPriority priority
) {
    return batch_recovery_.retry(plan_id, sample_id, priority);
}

BatchResultsOverview ProjectWorkspaceIntegrationService::results(const std::string_view plan_id) {
    return batch_results_.overview(plan_id);
}

}  // namespace biocore::application
