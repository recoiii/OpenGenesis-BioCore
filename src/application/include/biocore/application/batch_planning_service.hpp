#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/batch_plan.hpp"
#include "biocore/application/workflow_template_instantiation_service.hpp"
#include "biocore/domain/workflow.hpp"

namespace biocore::application {

class IBatchPlanStore;
class IManagedFileRepository;
class IPluginRegistry;
class IProjectSampleBindingStore;
class IProjectSampleStore;
class IWorkflowTemplateCatalog;
class SampleBindingService;

struct BatchWorkflowInputAssignment final {
    domain::WorkflowNodeId node_id;
    std::string input_port;
    BatchInputFileRole role{BatchInputFileRole::primary};
};

struct BatchPlanRequest final {
    std::string plan_id;
    std::string project_id;
    std::string template_id;
    std::string template_version;
    std::vector<std::string> sample_ids;
    std::vector<WorkflowTemplateParameterOverride> parameter_overrides;
    std::vector<BatchWorkflowInputAssignment> input_assignments;
};

class BatchPlanningService final {
public:
    static constexpr std::size_t maximum_selected_samples = 10000U;

    BatchPlanningService(
        IProjectSampleStore& samples,
        IProjectSampleBindingStore& bindings,
        IManagedFileRepository& managed_files,
        SampleBindingService& binding_validation,
        const IWorkflowTemplateCatalog& templates,
        const IPluginRegistry& plugins,
        IBatchPlanStore& plans
    ) noexcept;

    // Read-only. Produces a deterministic, per-sample run-plan preview.
    [[nodiscard]] BatchPlanPreview preview(const BatchPlanRequest& request);

    // Approves exactly the supplied preview snapshot. Current included bindings/files are
    // revalidated, but the workflow template is deliberately not reloaded: approval freezes
    // the exact plan the user previewed.
    [[nodiscard]] ApprovedBatchPlan approve(
        const BatchPlanPreview& preview,
        const std::vector<std::string>& excluded_sample_ids,
        std::string_view approved_at_utc
    );

private:
    IProjectSampleStore& samples_;
    IProjectSampleBindingStore& bindings_;
    IManagedFileRepository& managed_files_;
    SampleBindingService& binding_validation_;
    const IWorkflowTemplateCatalog& templates_;
    const IPluginRegistry& plugins_;
    IBatchPlanStore& plans_;
};

}  // namespace biocore::application
