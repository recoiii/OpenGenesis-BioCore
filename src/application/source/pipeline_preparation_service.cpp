#include "biocore/application/pipeline_preparation_service.hpp"

#include <stdexcept>
#include <utility>

#include "biocore/application/pipeline_planner.hpp"
#include "biocore/application/plugin_binding_resolver.hpp"

namespace biocore::application {

PipelinePreparationService::PipelinePreparationService(
    IExecutionPlanStore& store,
    const IPluginRegistry& plugin_registry
) noexcept
    : store_{store}, plugin_registry_{plugin_registry} {}

PipelinePreparationService::PipelinePreparationService(
    IExecutionPlanStore& store,
    const IPluginRegistry& plugin_registry,
    IManagedFileRepository& managed_files
) noexcept
    : store_{store}, plugin_registry_{plugin_registry}, managed_files_{&managed_files} {}

PreparedExecutionPlan PipelinePreparationService::prepare(
    const domain::PipelineDefinition& definition,
    const std::string_view job_id,
    const std::int64_t job_revision
) {
    return prepare(definition, job_id, job_revision, PipelineRunBindings{});
}

PreparedExecutionPlan PipelinePreparationService::prepare(
    const domain::PipelineDefinition& definition,
    const std::string_view job_id,
    const std::int64_t job_revision,
    const PipelineRunBindings& bindings
) {
    ExecutionPlan plan = PipelinePlanner::create_execution_plan(
        definition, job_id, job_revision, plugin_registry_
    );

    bool requires_binding_resolution = !bindings.steps.empty();
    if (!requires_binding_resolution) {
        for (const auto& step : plan.steps()) {
            requires_binding_resolution = !step.parameter_definitions.empty() ||
                                          !step.input_definitions.empty() ||
                                          !step.output_definitions.empty();
            if (requires_binding_resolution) break;
        }
    }
    if (requires_binding_resolution) {
        if (managed_files_ == nullptr) {
            throw std::logic_error(
                "Pipeline binding resolution requires a managed-file repository"
            );
        }
        plan = PluginBindingResolver::resolve(plan, bindings, *managed_files_);
    }

    std::string snapshot_path = store_.store(plan);
    return PreparedExecutionPlan{
        .plan = std::move(plan),
        .snapshot_path = std::move(snapshot_path),
    };
}

}  // namespace biocore::application
