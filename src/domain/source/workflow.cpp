#include "biocore/domain/workflow.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "biocore/domain/component_identity.hpp"

namespace biocore::domain {
namespace {

[[nodiscard]] bool is_blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
               return std::isspace(static_cast<unsigned char>(character)) != 0;
           });
}

void require_text(
    const std::string_view value,
    const std::string_view field,
    const std::size_t maximum_length
) {
    if (is_blank(value) || value.size() > maximum_length ||
        value.find('\0') != std::string_view::npos ||
        std::ranges::any_of(value, [](const char character) {
            return std::iscntrl(static_cast<unsigned char>(character)) != 0;
        })) {
        throw std::invalid_argument(std::string{field} + " is invalid");
    }
}

void require_optional_text(
    const std::string_view value,
    const std::string_view field,
    const std::size_t maximum_length
) {
    if (value.size() > maximum_length || value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string{field} + " is invalid");
    }
}

[[nodiscard]] bool valid_port_name(
    const std::string_view value,
    const std::size_t maximum_length
) {
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

void require_port_name(
    const std::string_view value,
    const std::string_view field,
    const std::size_t maximum_length
) {
    if (!valid_port_name(value, maximum_length)) {
        throw std::invalid_argument(std::string{field} + " is invalid");
    }
}

template <typename Declaration>
void require_unique_ports(
    const std::vector<Declaration>& declarations,
    const std::string_view field
) {
    std::unordered_set<std::string> names;
    names.reserve(declarations.size());
    for (const Declaration& declaration : declarations) {
        if (!names.emplace(declaration.name()).second) {
            throw std::invalid_argument(std::string{field} + " contains duplicate port names");
        }
    }
}

}  // namespace

WorkflowId::WorkflowId(std::string value) : value_{std::move(value)} {
    if (!is_namespaced_identifier(value_, maximum_length)) {
        throw std::invalid_argument("Workflow id is invalid");
    }
}

std::string_view WorkflowId::value() const noexcept { return value_; }

WorkflowNodeId::WorkflowNodeId(std::string value) : value_{std::move(value)} {
    if (!is_namespaced_identifier(value_, maximum_length)) {
        throw std::invalid_argument("Workflow node id is invalid");
    }
}

std::string_view WorkflowNodeId::value() const noexcept { return value_; }

WorkflowInputDeclaration::WorkflowInputDeclaration(
    std::string name,
    std::string artifact_type,
    const bool required
)
    : name_{std::move(name)},
      artifact_type_{std::move(artifact_type)},
      required_{required} {
    require_port_name(name_, "Workflow input name", maximum_name_length);
    require_text(artifact_type_, "Workflow input artifact type", maximum_artifact_type_length);
}

std::string_view WorkflowInputDeclaration::name() const noexcept { return name_; }
std::string_view WorkflowInputDeclaration::artifact_type() const noexcept { return artifact_type_; }
bool WorkflowInputDeclaration::required() const noexcept { return required_; }

WorkflowOutputDeclaration::WorkflowOutputDeclaration(
    std::string name,
    std::string artifact_type
)
    : name_{std::move(name)}, artifact_type_{std::move(artifact_type)} {
    require_port_name(name_, "Workflow output name", maximum_name_length);
    require_text(artifact_type_, "Workflow output artifact type", maximum_artifact_type_length);
}

std::string_view WorkflowOutputDeclaration::name() const noexcept { return name_; }
std::string_view WorkflowOutputDeclaration::artifact_type() const noexcept { return artifact_type_; }

WorkflowNode::WorkflowNode(
    WorkflowNodeId id,
    std::string label,
    std::string module_id,
    std::string plugin_version,
    std::vector<WorkflowInputDeclaration> inputs,
    std::vector<WorkflowOutputDeclaration> outputs,
    WorkflowParameters parameters
)
    : id_{std::move(id)},
      label_{std::move(label)},
      module_id_{std::move(module_id)},
      plugin_version_{std::move(plugin_version)},
      inputs_{std::move(inputs)},
      outputs_{std::move(outputs)},
      parameters_{std::move(parameters)} {
    require_text(label_, "Workflow node label", maximum_label_length);
    if (!is_namespaced_identifier(module_id_, maximum_module_id_length)) {
        throw std::invalid_argument("Workflow node module id is invalid");
    }
    if (!is_semantic_version(plugin_version_, maximum_plugin_version_length)) {
        throw std::invalid_argument("Workflow node plugin version must use semantic versioning");
    }
    if (inputs_.size() > maximum_ports || outputs_.size() > maximum_ports) {
        throw std::invalid_argument("Workflow node contains too many ports");
    }
    require_unique_ports(inputs_, "Workflow node inputs");
    require_unique_ports(outputs_, "Workflow node outputs");
    if (parameters_.size() > maximum_parameters) {
        throw std::invalid_argument("Workflow node contains too many parameters");
    }
    for (const auto& [name, value] : parameters_) {
        require_port_name(name, "Workflow parameter name", maximum_parameter_name_length);
        require_optional_text(value, "Workflow parameter value", maximum_parameter_value_length);
    }
}

const WorkflowNodeId& WorkflowNode::id() const noexcept { return id_; }
std::string_view WorkflowNode::label() const noexcept { return label_; }
std::string_view WorkflowNode::module_id() const noexcept { return module_id_; }
std::string_view WorkflowNode::plugin_version() const noexcept { return plugin_version_; }
const std::vector<WorkflowInputDeclaration>& WorkflowNode::inputs() const noexcept { return inputs_; }
const std::vector<WorkflowOutputDeclaration>& WorkflowNode::outputs() const noexcept { return outputs_; }
const WorkflowParameters& WorkflowNode::parameters() const noexcept { return parameters_; }

WorkflowEdge::WorkflowEdge(
    WorkflowNodeId source_node_id,
    std::string source_output,
    WorkflowNodeId target_node_id,
    std::string target_input
)
    : source_node_id_{std::move(source_node_id)},
      source_output_{std::move(source_output)},
      target_node_id_{std::move(target_node_id)},
      target_input_{std::move(target_input)} {
    require_port_name(
        source_output_, "Workflow edge source output", WorkflowOutputDeclaration::maximum_name_length
    );
    require_port_name(
        target_input_, "Workflow edge target input", WorkflowInputDeclaration::maximum_name_length
    );
}

const WorkflowNodeId& WorkflowEdge::source_node_id() const noexcept { return source_node_id_; }
std::string_view WorkflowEdge::source_output() const noexcept { return source_output_; }
const WorkflowNodeId& WorkflowEdge::target_node_id() const noexcept { return target_node_id_; }
std::string_view WorkflowEdge::target_input() const noexcept { return target_input_; }

Workflow::Workflow(
    const std::uint32_t schema_version,
    WorkflowId id,
    std::string name,
    std::string description,
    std::vector<WorkflowNode> nodes,
    std::vector<WorkflowEdge> edges
)
    : schema_version_{schema_version},
      id_{std::move(id)},
      name_{std::move(name)},
      description_{std::move(description)},
      nodes_{std::move(nodes)},
      edges_{std::move(edges)} {
    if (schema_version_ != current_schema_version) {
        throw std::invalid_argument("Workflow schema version is unsupported");
    }
    require_text(name_, "Workflow name", maximum_name_length);
    require_optional_text(description_, "Workflow description", maximum_description_length);
    if (nodes_.size() > maximum_nodes) {
        throw std::invalid_argument("Workflow contains too many nodes");
    }
    if (edges_.size() > maximum_edges) {
        throw std::invalid_argument("Workflow contains too many edges");
    }

    std::unordered_set<std::string> node_ids;
    node_ids.reserve(nodes_.size());
    for (const WorkflowNode& node : nodes_) {
        if (!node_ids.emplace(node.id().value()).second) {
            throw std::invalid_argument("Workflow contains duplicate node identifiers");
        }
    }
}

std::uint32_t Workflow::schema_version() const noexcept { return schema_version_; }
const WorkflowId& Workflow::id() const noexcept { return id_; }
std::string_view Workflow::name() const noexcept { return name_; }
std::string_view Workflow::description() const noexcept { return description_; }
const std::vector<WorkflowNode>& Workflow::nodes() const noexcept { return nodes_; }
const std::vector<WorkflowEdge>& Workflow::edges() const noexcept { return edges_; }

}  // namespace biocore::domain
