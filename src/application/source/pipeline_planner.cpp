#include "biocore/application/pipeline_planner.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace biocore::application {

ExecutionPlan PipelinePlanner::create_execution_plan(
    const domain::PipelineDefinition& definition,
    const std::string_view job_id,
    const std::int64_t job_revision,
    const IPluginRegistry& plugin_registry
) {
    std::vector<ExecutionPlanStep> planned_steps;
    planned_steps.reserve(definition.steps().size());
    for (const std::size_t index : definition.topological_order()) {
        const domain::PipelineStep& step = definition.steps().at(index);
        const auto resolved = plugin_registry.find_module(step.module_id());
        if (!resolved.has_value()) {
            throw std::invalid_argument(
                "Pipeline references an unavailable plugin module: " +
                std::string{step.module_id()}
            );
        }
        planned_steps.push_back(ExecutionPlanStep{
            .id = std::string{step.id()},
            .module_id = std::string{step.module_id()},
            .plugin_id = resolved->plugin_id,
            .plugin_version = resolved->plugin_version,
            .module_type = resolved->module_type,
            .plugin_root_path = resolved->plugin_root_path,
            .executable_path = resolved->executable_path,
            .depends_on = step.depends_on(),
            .normalized_weight = step.weight() / definition.total_weight(),
            .parameter_definitions = resolved->parameters,
            .input_definitions = resolved->inputs,
            .output_definitions = resolved->outputs,
            .parameters = {},
            .inputs = {},
            .outputs = {},
        });
    }

    return ExecutionPlan{
        ExecutionPlan::current_schema_version,
        std::string{job_id},
        job_revision,
        std::string{definition.id()},
        std::string{definition.version()},
        std::move(planned_steps),
    };
}

}  // namespace biocore::application
