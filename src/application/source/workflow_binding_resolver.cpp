#include "biocore/application/workflow_binding_resolver.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <tuple>
#include <utility>

#include "biocore/domain/workflow_dag.hpp"

namespace biocore::application {
namespace {

using NodeLookup = std::map<std::string, const domain::WorkflowNode*, std::less<>>;
using ModuleLookup = std::map<std::string, ResolvedPluginModule, std::less<>>;
using ParameterKey = std::pair<std::string, std::string>;
using InputKey = std::pair<std::string, std::string>;
using EdgeSource = std::pair<std::string, std::string>;

[[nodiscard]] bool valid_binding_name(
    const std::string_view value,
    const std::size_t maximum_length = 128U
) noexcept {
    if (value.empty() || value.size() > maximum_length ||
        value.find('\0') != std::string_view::npos ||
        !(value.front() >= 'a' && value.front() <= 'z')) {
        return false;
    }
    return std::ranges::all_of(value, [](const char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') ||
               character == '-' || character == '_';
    });
}

[[nodiscard]] bool valid_text(
    const std::string_view value,
    const std::size_t maximum_length = 4096U
) noexcept {
    return !value.empty() && value.size() <= maximum_length &&
           value.find('\0') == std::string_view::npos &&
           std::ranges::none_of(value, [](const char character) {
               return std::iscntrl(static_cast<unsigned char>(character)) != 0;
           });
}

[[noreturn]] void fail(
    const WorkflowBindingErrorCode code,
    const std::string& message
) {
    throw WorkflowBindingError{code, "Workflow binding failed: " + message};
}

[[nodiscard]] const domain::PluginInputPortDefinition* find_module_input(
    const ResolvedPluginModule& module,
    const std::string_view name
) noexcept {
    const auto iterator = std::ranges::find_if(
        module.inputs, [name](const auto& input) { return input.name() == name; }
    );
    return iterator == module.inputs.end() ? nullptr : &*iterator;
}

[[nodiscard]] const domain::PluginOutputPortDefinition* find_module_output(
    const ResolvedPluginModule& module,
    const std::string_view name
) noexcept {
    const auto iterator = std::ranges::find_if(
        module.outputs, [name](const auto& output) { return output.name() == name; }
    );
    return iterator == module.outputs.end() ? nullptr : &*iterator;
}

[[nodiscard]] const domain::PluginParameterDefinition* find_module_parameter(
    const ResolvedPluginModule& module,
    const std::string_view name
) noexcept {
    const auto iterator = std::ranges::find_if(
        module.parameters, [name](const auto& parameter) { return parameter.name() == name; }
    );
    return iterator == module.parameters.end() ? nullptr : &*iterator;
}

[[nodiscard]] const domain::WorkflowInputDeclaration* find_workflow_input(
    const domain::WorkflowNode& node,
    const std::string_view name
) noexcept {
    const auto iterator = std::ranges::find_if(
        node.inputs(), [name](const auto& input) { return input.name() == name; }
    );
    return iterator == node.inputs().end() ? nullptr : &*iterator;
}

[[nodiscard]] const domain::WorkflowOutputDeclaration* find_workflow_output(
    const domain::WorkflowNode& node,
    const std::string_view name
) noexcept {
    const auto iterator = std::ranges::find_if(
        node.outputs(), [name](const auto& output) { return output.name() == name; }
    );
    return iterator == node.outputs().end() ? nullptr : &*iterator;
}

void validate_node_contract(
    const domain::WorkflowNode& node,
    const ResolvedPluginModule& module
) {
    for (const domain::WorkflowInputDeclaration& input : node.inputs()) {
        const auto* definition = find_module_input(module, input.name());
        if (definition == nullptr) {
            fail(
                WorkflowBindingErrorCode::unknown_declared_input,
                "node '" + std::string{node.id().value()} +
                    "' declares unknown module input '" + std::string{input.name()} + "'"
            );
        }
        if (!definition->accepts_file_type(input.artifact_type())) {
            fail(
                WorkflowBindingErrorCode::incompatible_declared_input_type,
                "node '" + std::string{node.id().value()} + "' declares artifact type '" +
                    std::string{input.artifact_type()} + "' for input '" +
                    std::string{input.name()} + "', which the module does not accept"
            );
        }
        if (definition->required() && !input.required()) {
            fail(
                WorkflowBindingErrorCode::required_input_declaration_missing,
                "node '" + std::string{node.id().value()} + "' weakens required module input '" +
                    std::string{input.name()} + "'"
            );
        }
    }

    for (const auto& definition : module.inputs) {
        if (definition.required() && find_workflow_input(node, definition.name()) == nullptr) {
            fail(
                WorkflowBindingErrorCode::required_input_declaration_missing,
                "node '" + std::string{node.id().value()} +
                    "' omits required module input '" + std::string{definition.name()} + "'"
            );
        }
    }

    for (const domain::WorkflowOutputDeclaration& output : node.outputs()) {
        const auto* definition = find_module_output(module, output.name());
        if (definition == nullptr) {
            fail(
                WorkflowBindingErrorCode::unknown_declared_output,
                "node '" + std::string{node.id().value()} +
                    "' declares unknown module output '" + std::string{output.name()} + "'"
            );
        }
        if (definition->file_type() != "*" &&
            definition->file_type() != output.artifact_type()) {
            fail(
                WorkflowBindingErrorCode::incompatible_declared_output_type,
                "node '" + std::string{node.id().value()} + "' declares artifact type '" +
                    std::string{output.artifact_type()} + "' for output '" +
                    std::string{output.name()} + "', but the module produces '" +
                    std::string{definition->file_type()} + "'"
            );
        }
    }
}

[[nodiscard]] std::string canonical_parameter_value(
    const domain::PluginParameterDefinition& definition,
    const domain::PluginParameterValue& value
) {
    try {
        definition.validate_value(value);
        return domain::plugin_parameter_value_to_string(value, definition.type());
    } catch (const std::invalid_argument& error) {
        fail(
            WorkflowBindingErrorCode::invalid_parameter_value,
            "parameter '" + std::string{definition.name()} + "' is invalid: " + error.what()
        );
    }
}

[[nodiscard]] std::string canonical_literal_parameter(
    const domain::PluginParameterDefinition& definition,
    const std::string_view literal
) {
    try {
        const auto value =
            domain::plugin_parameter_value_from_string(literal, definition.type());
        definition.validate_value(value);
        return domain::plugin_parameter_value_to_string(value, definition.type());
    } catch (const std::invalid_argument& error) {
        fail(
            WorkflowBindingErrorCode::invalid_parameter_value,
            "literal for parameter '" + std::string{definition.name()} +
                "' is invalid: " + error.what()
        );
    }
}

}  // namespace

std::string_view to_string(const WorkflowBindingErrorCode code) noexcept {
    switch (code) {
        case WorkflowBindingErrorCode::module_not_found: return "module_not_found";
        case WorkflowBindingErrorCode::plugin_version_mismatch: return "plugin_version_mismatch";
        case WorkflowBindingErrorCode::unknown_declared_input: return "unknown_declared_input";
        case WorkflowBindingErrorCode::unknown_declared_output: return "unknown_declared_output";
        case WorkflowBindingErrorCode::incompatible_declared_input_type:
            return "incompatible_declared_input_type";
        case WorkflowBindingErrorCode::incompatible_declared_output_type:
            return "incompatible_declared_output_type";
        case WorkflowBindingErrorCode::required_input_declaration_missing:
            return "required_input_declaration_missing";
        case WorkflowBindingErrorCode::unknown_node_parameter: return "unknown_node_parameter";
        case WorkflowBindingErrorCode::unknown_binding_node: return "unknown_binding_node";
        case WorkflowBindingErrorCode::unknown_binding_parameter:
            return "unknown_binding_parameter";
        case WorkflowBindingErrorCode::unknown_workflow_parameter:
            return "unknown_workflow_parameter";
        case WorkflowBindingErrorCode::duplicate_workflow_parameter:
            return "duplicate_workflow_parameter";
        case WorkflowBindingErrorCode::parameter_source_conflict:
            return "parameter_source_conflict";
        case WorkflowBindingErrorCode::invalid_parameter_value:
            return "invalid_parameter_value";
        case WorkflowBindingErrorCode::required_parameter_missing:
            return "required_parameter_missing";
        case WorkflowBindingErrorCode::duplicate_resource: return "duplicate_resource";
        case WorkflowBindingErrorCode::unknown_resource: return "unknown_resource";
        case WorkflowBindingErrorCode::unknown_binding_input: return "unknown_binding_input";
        case WorkflowBindingErrorCode::duplicate_binding_reference:
            return "duplicate_binding_reference";
        case WorkflowBindingErrorCode::input_source_conflict: return "input_source_conflict";
        case WorkflowBindingErrorCode::incompatible_artifact_type:
            return "incompatible_artifact_type";
        case WorkflowBindingErrorCode::required_input_missing: return "required_input_missing";
    }
    return "unknown";
}

WorkflowBindingError::WorkflowBindingError(
    const WorkflowBindingErrorCode code,
    std::string message
)
    : std::invalid_argument{std::move(message)}, code_{code} {}

WorkflowBindingErrorCode WorkflowBindingError::code() const noexcept { return code_; }

std::string_view to_string(const ResolvedWorkflowParameterSource source) noexcept {
    switch (source) {
        case ResolvedWorkflowParameterSource::node_literal: return "node_literal";
        case ResolvedWorkflowParameterSource::workflow_parameter: return "workflow_parameter";
        case ResolvedWorkflowParameterSource::module_default: return "module_default";
    }
    return "unknown";
}

std::string_view to_string(const ResolvedWorkflowInputSourceKind kind) noexcept {
    switch (kind) {
        case ResolvedWorkflowInputSourceKind::workflow_resource: return "workflow_resource";
        case ResolvedWorkflowInputSourceKind::node_output: return "node_output";
    }
    return "unknown";
}

WorkflowBindingPlan::WorkflowBindingPlan(
    domain::WorkflowId workflow_id,
    std::vector<ResolvedWorkflowNodeBinding> nodes
)
    : workflow_id_{std::move(workflow_id)}, nodes_{std::move(nodes)} {}

const domain::WorkflowId& WorkflowBindingPlan::workflow_id() const noexcept {
    return workflow_id_;
}

const std::vector<ResolvedWorkflowNodeBinding>& WorkflowBindingPlan::nodes() const noexcept {
    return nodes_;
}

WorkflowBindingPlan WorkflowBindingResolver::resolve(
    const domain::Workflow& workflow,
    const WorkflowBindingRequest& request,
    const IPluginRegistry& registry
) {
    const domain::WorkflowDagPlan dag_plan = domain::plan_workflow_dag(workflow);

    NodeLookup nodes;
    for (const domain::WorkflowNode& node : workflow.nodes()) {
        nodes.emplace(std::string{node.id().value()}, &node);
    }

    ModuleLookup modules;
    for (const auto& [node_id, node] : nodes) {
        const auto module = registry.find_module(node->module_id());
        if (!module.has_value()) {
            fail(
                WorkflowBindingErrorCode::module_not_found,
                "module '" + std::string{node->module_id()} +
                    "' for node '" + node_id + "' was not found"
            );
        }
        if (module->plugin_version != node->plugin_version()) {
            fail(
                WorkflowBindingErrorCode::plugin_version_mismatch,
                "node '" + node_id + "' requires plugin version '" +
                    std::string{node->plugin_version()} + "' but registry resolved '" +
                    module->plugin_version + "'"
            );
        }
        validate_node_contract(*node, *module);
        modules.emplace(node_id, *module);
    }

    std::map<std::string, domain::PluginParameterValue, std::less<>> workflow_parameters;
    for (const WorkflowParameterBinding& parameter : request.parameters) {
        if (!valid_binding_name(parameter.name)) {
            fail(
                WorkflowBindingErrorCode::unknown_workflow_parameter,
                "workflow parameter name is invalid"
            );
        }
        if (!workflow_parameters.emplace(parameter.name, parameter.value).second) {
            fail(
                WorkflowBindingErrorCode::duplicate_workflow_parameter,
                "workflow parameter '" + parameter.name + "' is supplied more than once"
            );
        }
    }

    std::map<std::string, WorkflowArtifactResource, std::less<>> resources;
    for (const WorkflowArtifactResource& resource : request.resources) {
        if (!valid_binding_name(resource.name) ||
            !valid_text(resource.resource_id, 512U) ||
            !valid_text(resource.artifact_type, 128U)) {
            fail(
                WorkflowBindingErrorCode::duplicate_resource,
                "workflow resource declaration is invalid"
            );
        }
        if (!resources.emplace(resource.name, resource).second) {
            fail(
                WorkflowBindingErrorCode::duplicate_resource,
                "workflow resource '" + resource.name + "' is supplied more than once"
            );
        }
    }

    std::map<ParameterKey, std::string> parameter_references;
    for (const WorkflowNodeParameterReference& reference : request.parameter_references) {
        const std::string node_id{reference.node_id.value()};
        const auto node_iterator = nodes.find(node_id);
        if (node_iterator == nodes.end()) {
            fail(
                WorkflowBindingErrorCode::unknown_binding_node,
                "parameter reference targets unknown node '" + node_id + "'"
            );
        }
        const ResolvedPluginModule& module = modules.at(node_id);
        if (find_module_parameter(module, reference.parameter_name) == nullptr) {
            fail(
                WorkflowBindingErrorCode::unknown_binding_parameter,
                "parameter reference targets unknown parameter '" + node_id + "." +
                    reference.parameter_name + "'"
            );
        }
        if (!workflow_parameters.contains(reference.workflow_parameter_name)) {
            fail(
                WorkflowBindingErrorCode::unknown_workflow_parameter,
                "parameter reference uses unknown workflow parameter '" +
                    reference.workflow_parameter_name + "'"
            );
        }
        const ParameterKey key{node_id, reference.parameter_name};
        if (!parameter_references.emplace(key, reference.workflow_parameter_name).second) {
            fail(
                WorkflowBindingErrorCode::duplicate_binding_reference,
                "node parameter '" + node_id + "." + reference.parameter_name +
                    "' has multiple workflow-parameter references"
            );
        }
    }

    std::map<InputKey, std::string> resource_references;
    for (const WorkflowNodeResourceReference& reference : request.resource_references) {
        const std::string node_id{reference.node_id.value()};
        const auto node_iterator = nodes.find(node_id);
        if (node_iterator == nodes.end()) {
            fail(
                WorkflowBindingErrorCode::unknown_binding_node,
                "resource reference targets unknown node '" + node_id + "'"
            );
        }
        if (find_workflow_input(*node_iterator->second, reference.input_port) == nullptr) {
            fail(
                WorkflowBindingErrorCode::unknown_binding_input,
                "resource reference targets unknown workflow input '" + node_id + "." +
                    reference.input_port + "'"
            );
        }
        if (!resources.contains(reference.resource_name)) {
            fail(
                WorkflowBindingErrorCode::unknown_resource,
                "resource reference uses unknown workflow resource '" +
                    reference.resource_name + "'"
            );
        }
        const InputKey key{node_id, reference.input_port};
        if (!resource_references.emplace(key, reference.resource_name).second) {
            fail(
                WorkflowBindingErrorCode::duplicate_binding_reference,
                "workflow input '" + node_id + "." + reference.input_port +
                    "' has multiple resource references"
            );
        }
    }

    std::map<InputKey, EdgeSource> edge_sources;
    for (const domain::WorkflowEdge& edge : workflow.edges()) {
        edge_sources.emplace(
            InputKey{
                std::string{edge.target_node_id().value()},
                std::string{edge.target_input()}
            },
            EdgeSource{
                std::string{edge.source_node_id().value()},
                std::string{edge.source_output()}
            }
        );
    }

    std::vector<ResolvedWorkflowNodeBinding> resolved_nodes;
    resolved_nodes.reserve(dag_plan.ordered_nodes().size());

    for (const domain::WorkflowPlanNode& planned : dag_plan.ordered_nodes()) {
        const std::string node_id{planned.id.value()};
        const domain::WorkflowNode& node = *nodes.at(node_id);
        const ResolvedPluginModule& module = modules.at(node_id);

        ResolvedWorkflowNodeBinding resolved{
            .node_id = planned.id,
            .module_id = std::string{node.module_id()},
            .plugin_version = std::string{node.plugin_version()},
            .parameters = {},
            .inputs = {},
            .outputs = {},
        };

        std::set<std::string, std::less<>> supplied_parameters;

        for (const auto& [name, literal] : node.parameters()) {
            const auto* definition = find_module_parameter(module, name);
            if (definition == nullptr) {
                fail(
                    WorkflowBindingErrorCode::unknown_node_parameter,
                    "node '" + node_id + "' supplies unknown parameter '" + name + "'"
                );
            }
            const ParameterKey key{node_id, name};
            if (parameter_references.contains(key)) {
                fail(
                    WorkflowBindingErrorCode::parameter_source_conflict,
                    "node parameter '" + node_id + "." + name +
                        "' has both a literal and workflow-parameter reference"
                );
            }
            resolved.parameters.push_back(ResolvedWorkflowParameter{
                .name = name,
                .type = definition->type(),
                .value = canonical_literal_parameter(*definition, literal),
                .source = ResolvedWorkflowParameterSource::node_literal,
            });
            supplied_parameters.insert(name);
        }

        for (const auto& [key, workflow_parameter_name] : parameter_references) {
            if (key.first != node_id) continue;
            const auto* definition = find_module_parameter(module, key.second);
            if (definition == nullptr) {
                fail(
                    WorkflowBindingErrorCode::unknown_binding_parameter,
                    "node parameter reference became unresolved"
                );
            }
            const auto& value = workflow_parameters.at(workflow_parameter_name);
            resolved.parameters.push_back(ResolvedWorkflowParameter{
                .name = key.second,
                .type = definition->type(),
                .value = canonical_parameter_value(*definition, value),
                .source = ResolvedWorkflowParameterSource::workflow_parameter,
            });
            supplied_parameters.insert(key.second);
        }

        for (const auto& definition : module.parameters) {
            if (supplied_parameters.contains(definition.name())) continue;
            if (definition.default_value().has_value()) {
                resolved.parameters.push_back(ResolvedWorkflowParameter{
                    .name = std::string{definition.name()},
                    .type = definition.type(),
                    .value = canonical_parameter_value(
                        definition, *definition.default_value()
                    ),
                    .source = ResolvedWorkflowParameterSource::module_default,
                });
            } else if (definition.required()) {
                fail(
                    WorkflowBindingErrorCode::required_parameter_missing,
                    "required parameter '" + node_id + "." +
                        std::string{definition.name()} + "' is unresolved"
                );
            }
        }

        std::ranges::sort(
            resolved.parameters,
            [](const auto& left, const auto& right) { return left.name < right.name; }
        );

        for (const domain::WorkflowInputDeclaration& input : node.inputs()) {
            const InputKey key{node_id, std::string{input.name()}};
            const auto edge = edge_sources.find(key);
            const auto resource_reference = resource_references.find(key);

            if (edge != edge_sources.end() &&
                resource_reference != resource_references.end()) {
                fail(
                    WorkflowBindingErrorCode::input_source_conflict,
                    "workflow input '" + node_id + "." + std::string{input.name()} +
                        "' has both an upstream edge and workflow-resource binding"
                );
            }

            if (edge != edge_sources.end()) {
                const domain::WorkflowNode& source_node = *nodes.at(edge->second.first);
                const auto* source_output =
                    find_workflow_output(source_node, edge->second.second);
                if (source_output == nullptr) {
                    throw std::logic_error("Validated DAG source output became unavailable");
                }
                if (source_output->artifact_type() != input.artifact_type()) {
                    fail(
                        WorkflowBindingErrorCode::incompatible_artifact_type,
                        "edge artifact type '" +
                            std::string{source_output->artifact_type()} +
                            "' is incompatible with target declaration '" +
                            std::string{input.artifact_type()} + "' at '" + node_id +
                            "." + std::string{input.name()} + "'"
                    );
                }
                resolved.inputs.push_back(ResolvedWorkflowInputBinding{
                    .port_name = std::string{input.name()},
                    .artifact_type = std::string{source_output->artifact_type()},
                    .source_kind = ResolvedWorkflowInputSourceKind::node_output,
                    .source_id = edge->second.first,
                    .source_port = edge->second.second,
                });
                continue;
            }

            if (resource_reference != resource_references.end()) {
                const WorkflowArtifactResource& resource =
                    resources.at(resource_reference->second);
                if (resource.artifact_type != input.artifact_type()) {
                    fail(
                        WorkflowBindingErrorCode::incompatible_artifact_type,
                        "resource artifact type '" + resource.artifact_type +
                            "' is incompatible with target declaration '" +
                            std::string{input.artifact_type()} + "' at '" + node_id +
                            "." + std::string{input.name()} + "'"
                    );
                }
                resolved.inputs.push_back(ResolvedWorkflowInputBinding{
                    .port_name = std::string{input.name()},
                    .artifact_type = resource.artifact_type,
                    .source_kind = ResolvedWorkflowInputSourceKind::workflow_resource,
                    .source_id = resource.resource_id,
                    .source_port = {},
                });
                continue;
            }

            const auto* module_input = find_module_input(module, input.name());
            const bool required =
                input.required() || (module_input != nullptr && module_input->required());
            if (required) {
                fail(
                    WorkflowBindingErrorCode::required_input_missing,
                    "required workflow input '" + node_id + "." +
                        std::string{input.name()} + "' is unresolved"
                );
            }
        }

        std::ranges::sort(
            resolved.inputs,
            [](const auto& left, const auto& right) {
                return left.port_name < right.port_name;
            }
        );

        for (const domain::WorkflowOutputDeclaration& output : node.outputs()) {
            resolved.outputs.push_back(ResolvedWorkflowOutputBinding{
                .port_name = std::string{output.name()},
                .artifact_type = std::string{output.artifact_type()},
            });
        }
        std::ranges::sort(
            resolved.outputs,
            [](const auto& left, const auto& right) {
                return left.port_name < right.port_name;
            }
        );

        resolved_nodes.push_back(std::move(resolved));
    }

    return WorkflowBindingPlan{workflow.id(), std::move(resolved_nodes)};
}

}  // namespace biocore::application
