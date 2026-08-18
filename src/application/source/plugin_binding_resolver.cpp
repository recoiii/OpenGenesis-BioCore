#include "biocore/application/plugin_binding_resolver.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace biocore::application {
namespace {

[[nodiscard]] std::string safe_output_relative_path(
    const std::string_view job_id,
    const std::string_view step_id,
    const std::string_view port_name
) {
    return "outputs/" + std::string{job_id} + "--" + std::string{step_id} + "--" +
           std::string{port_name} + ".out";
}

[[nodiscard]] const PipelineStepBindings* find_step_bindings(
    const PipelineRunBindings& bindings,
    const std::string_view step_id
) {
    const auto iterator = std::ranges::find_if(bindings.steps, [step_id](const auto& item) {
        return item.step_id == step_id;
    });
    return iterator == bindings.steps.end() ? nullptr : &*iterator;
}

[[nodiscard]] const domain::PluginParameterDefinition* find_parameter(
    const ExecutionPlanStep& step,
    const std::string_view name
) {
    const auto iterator = std::ranges::find_if(step.parameter_definitions, [name](const auto& item) {
        return item.name() == name;
    });
    return iterator == step.parameter_definitions.end() ? nullptr : &*iterator;
}

[[nodiscard]] const domain::PluginInputPortDefinition* find_input(
    const ExecutionPlanStep& step,
    const std::string_view name
) {
    const auto iterator = std::ranges::find_if(step.input_definitions, [name](const auto& item) {
        return item.name() == name;
    });
    return iterator == step.input_definitions.end() ? nullptr : &*iterator;
}



}  // namespace

ExecutionPlan PluginBindingResolver::resolve(
    const ExecutionPlan& plan,
    const PipelineRunBindings& bindings,
    IManagedFileRepository& managed_files
) {
    std::unordered_set<std::string_view> known_steps;
    known_steps.reserve(plan.steps().size());
    for (const auto& step : plan.steps()) known_steps.insert(step.id);

    std::unordered_set<std::string_view> binding_step_ids;
    for (const auto& step_binding : bindings.steps) {
        if (!known_steps.contains(step_binding.step_id)) {
            throw std::invalid_argument("Pipeline bindings reference an unknown step");
        }
        if (!binding_step_ids.insert(step_binding.step_id).second) {
            throw std::invalid_argument("Pipeline bindings contain duplicate step entries");
        }
    }

    std::vector<ExecutionPlanStep> resolved = plan.steps();
    std::unordered_map<std::string, std::unordered_map<std::string, ExecutionOutputBinding>> outputs;
    outputs.reserve(resolved.size());

    for (ExecutionPlanStep& step : resolved) {
        const PipelineStepBindings* step_bindings = find_step_bindings(bindings, step.id);
        std::unordered_set<std::string_view> supplied_parameters;
        std::unordered_set<std::string_view> supplied_inputs;

        if (step_bindings != nullptr) {
            for (const auto& binding : step_bindings->parameters) {
                const auto* definition = find_parameter(step, binding.name);
                if (definition == nullptr) {
                    throw std::invalid_argument("Pipeline bindings contain an unknown parameter");
                }
                if (!supplied_parameters.insert(binding.name).second) {
                    throw std::invalid_argument("Pipeline bindings contain a duplicate parameter");
                }
                definition->validate_value(binding.value);
                step.parameters.push_back(ExecutionParameterBinding{
                    .name = binding.name,
                    .type = definition->type(),
                    .value = domain::plugin_parameter_value_to_string(binding.value, definition->type()),
                });
            }
        }

        for (const auto& definition : step.parameter_definitions) {
            if (supplied_parameters.contains(definition.name())) continue;
            if (definition.default_value().has_value()) {
                step.parameters.push_back(ExecutionParameterBinding{
                    .name = std::string{definition.name()},
                    .type = definition.type(),
                    .value = domain::plugin_parameter_value_to_string(
                        *definition.default_value(), definition.type()
                    ),
                });
            } else if (definition.required()) {
                throw std::invalid_argument("Required plugin parameter is missing");
            }
        }

        if (step_bindings != nullptr) {
            for (const auto& binding : step_bindings->inputs) {
                const auto* definition = find_input(step, binding.port_name);
                if (definition == nullptr) {
                    throw std::invalid_argument("Pipeline bindings contain an unknown input port");
                }
                if (!supplied_inputs.insert(binding.port_name).second) {
                    throw std::invalid_argument("Pipeline bindings contain a duplicate input port");
                }

                if (const auto* managed = std::get_if<ManagedFileInputSource>(&binding.source)) {
                    const auto file = managed_files.find_by_id(managed->file_id);
                    if (!file.has_value() || !file->relative_project_path().has_value()) {
                        throw std::invalid_argument("Managed-file input binding cannot be resolved");
                    }
                    if (!definition->accepts_file_type(file->file_type())) {
                        throw std::invalid_argument("Managed-file input type is not accepted by plugin port");
                    }
                    step.inputs.push_back(ExecutionInputBinding{
                        .port_name = binding.port_name,
                        .source_kind = ExecutionInputSourceKind::managed_file,
                        .source_id = managed->file_id,
                        .file_type = std::string{file->file_type()},
                        .relative_project_path = *file->relative_project_path(),
                    });
                } else {
                    const auto& upstream = std::get<StepOutputInputSource>(binding.source);
                    if (!std::ranges::any_of(step.depends_on, [&upstream](const std::string& dependency) {
                            return dependency == upstream.step_id;
                        })) {
                        throw std::invalid_argument("Step-output input must reference a direct dependency");
                    }
                    const auto step_iterator = outputs.find(upstream.step_id);
                    if (step_iterator == outputs.end()) {
                        throw std::invalid_argument("Step-output input references an unavailable upstream output");
                    }
                    const auto port_iterator = step_iterator->second.find(upstream.output_port);
                    if (port_iterator == step_iterator->second.end()) {
                        throw std::invalid_argument("Step-output input references an unknown output port");
                    }
                    if (!definition->accepts_file_type(port_iterator->second.file_type)) {
                        throw std::invalid_argument("Upstream output type is not accepted by plugin port");
                    }
                    step.inputs.push_back(ExecutionInputBinding{
                        .port_name = binding.port_name,
                        .source_kind = ExecutionInputSourceKind::step_output,
                        .source_id = upstream.step_id + "." + upstream.output_port,
                        .file_type = port_iterator->second.file_type,
                        .relative_project_path = port_iterator->second.relative_project_path,
                    });
                }
            }
        }

        for (const auto& definition : step.input_definitions) {
            if (definition.required() && !supplied_inputs.contains(definition.name())) {
                throw std::invalid_argument("Required plugin input is missing");
            }
        }

        auto& step_outputs = outputs[step.id];
        for (const auto& definition : step.output_definitions) {
            ExecutionOutputBinding output{
                .port_name = std::string{definition.name()},
                .file_type = std::string{definition.file_type()},
                .relative_project_path = safe_output_relative_path(
                    plan.job_id(), step.id, definition.name()
                ),
            };
            step_outputs.emplace(output.port_name, output);
            step.outputs.push_back(std::move(output));
        }
    }

    return ExecutionPlan{
        plan.schema_version(), std::string{plan.job_id()}, plan.job_revision(),
        std::string{plan.pipeline_id()}, std::string{plan.pipeline_version()}, std::move(resolved)
    };
}

}  // namespace biocore::application
