#include "biocore/application/workflow_template_instantiation_service.hpp"

#include <map>
#include <set>
#include <utility>

namespace biocore::application {

std::string_view to_string(
    const WorkflowTemplateInstantiationErrorCode code
) noexcept {
    switch (code) {
        case WorkflowTemplateInstantiationErrorCode::template_not_found:
            return "template_not_found";
        case WorkflowTemplateInstantiationErrorCode::duplicate_parameter_override:
            return "duplicate_parameter_override";
        case WorkflowTemplateInstantiationErrorCode::unknown_override_node:
            return "unknown_override_node";
    }
    return "unknown";
}

WorkflowTemplateInstantiationError::WorkflowTemplateInstantiationError(
    const WorkflowTemplateInstantiationErrorCode code,
    std::string message
)
    : std::invalid_argument{std::move(message)}, code_{code} {}

WorkflowTemplateInstantiationErrorCode
WorkflowTemplateInstantiationError::code() const noexcept {
    return code_;
}

WorkflowTemplateInstantiationService::WorkflowTemplateInstantiationService(
    const IWorkflowTemplateCatalog& catalog
) noexcept
    : catalog_{catalog} {}

InstantiatedWorkflowTemplate WorkflowTemplateInstantiationService::instantiate(
    const InstantiateWorkflowTemplateRequest& request
) const {
    const auto value = catalog_.find(
        request.template_id, request.template_version
    );
    if (!value.has_value()) {
        throw WorkflowTemplateInstantiationError{
            WorkflowTemplateInstantiationErrorCode::template_not_found,
            "Workflow template id/version was not found"
        };
    }

    using OverrideKey = std::pair<std::string, std::string>;
    std::map<OverrideKey, std::string> overrides;
    for (const WorkflowTemplateParameterOverride& override_value :
         request.parameter_overrides) {
        const OverrideKey key{
            std::string{override_value.node_id.value()},
            override_value.parameter_name
        };
        if (!overrides.emplace(key, override_value.value).second) {
            throw WorkflowTemplateInstantiationError{
                WorkflowTemplateInstantiationErrorCode::duplicate_parameter_override,
                "Workflow template parameter override is duplicated"
            };
        }
    }

    std::set<std::string, std::less<>> known_nodes;
    std::vector<domain::WorkflowNode> nodes;
    nodes.reserve(value->blueprint().nodes().size());
    for (const domain::WorkflowNode& node : value->blueprint().nodes()) {
        const std::string node_id{node.id().value()};
        known_nodes.emplace(node_id);
        domain::WorkflowParameters parameters = node.parameters();

        for (const auto& [key, override_value] : overrides) {
            if (key.first == node_id) {
                parameters[key.second] = override_value;
            }
        }

        nodes.emplace_back(
            node.id(),
            std::string{node.label()},
            std::string{node.module_id()},
            std::string{node.plugin_version()},
            node.inputs(),
            node.outputs(),
            std::move(parameters)
        );
    }

    for (const auto& [key, override_value] : overrides) {
        static_cast<void>(override_value);
        if (!known_nodes.contains(key.first)) {
            throw WorkflowTemplateInstantiationError{
                WorkflowTemplateInstantiationErrorCode::unknown_override_node,
                "Workflow template parameter override targets an unknown node"
            };
        }
    }

    const std::string name = request.name.has_value()
        ? *request.name
        : std::string{value->name()};
    const std::string description = request.description.has_value()
        ? *request.description
        : std::string{value->description()};

    domain::Workflow workflow{
        domain::Workflow::current_schema_version,
        request.workflow_id,
        name,
        description,
        std::move(nodes),
        value->blueprint().edges()
    };

    return InstantiatedWorkflowTemplate{
        .template_id = std::string{value->id()},
        .template_version = std::string{value->version()},
        .workflow = std::move(workflow),
    };
}

}  // namespace biocore::application
