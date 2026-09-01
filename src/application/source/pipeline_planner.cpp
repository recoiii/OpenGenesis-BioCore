#include "biocore/application/pipeline_planner.hpp"

#include <stdexcept>
#include <string>
#include <vector>

#include "biocore/domain/plugin_manifest.hpp"

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
        if (resolved->module_id != step.module_id()) {
            throw std::invalid_argument("Plugin registry returned a mismatched module identity");
        }
        if (resolved->plugin_version != step.plugin_version()) {
            throw std::invalid_argument(
                "Pipeline plugin version pin does not match the discovered module: " +
                std::string{step.module_id()}
            );
        }
        if (resolved->plugin_manifest_version < domain::PluginManifest::minimum_manifest_version ||
            resolved->plugin_manifest_version > domain::PluginManifest::current_manifest_version) {
            throw std::invalid_argument("Resolved plugin manifest version is unsupported");
        }
        if (resolved->plugin_api_version != domain::PluginManifest::supported_api_version) {
            throw std::invalid_argument("Resolved plugin API version is unsupported");
        }
        if (!resolved->module_id.starts_with(resolved->plugin_id + '.')) {
            throw std::invalid_argument("Resolved plugin module is outside its plugin namespace");
        }
        planned_steps.push_back(ExecutionPlanStep{
            .id = std::string{step.id()},
            .module_id = std::string{step.module_id()},
            .plugin_id = resolved->plugin_id,
            .plugin_version = resolved->plugin_version,
            .plugin_manifest_version = resolved->plugin_manifest_version,
            .plugin_api_version = resolved->plugin_api_version,
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
