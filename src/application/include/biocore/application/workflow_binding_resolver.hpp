#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_plugin_registry.hpp"
#include "biocore/domain/plugin_io_contract.hpp"
#include "biocore/domain/workflow.hpp"

namespace biocore::application {

enum class WorkflowBindingErrorCode {
    module_not_found,
    plugin_version_mismatch,
    unknown_declared_input,
    unknown_declared_output,
    incompatible_declared_input_type,
    incompatible_declared_output_type,
    required_input_declaration_missing,
    unknown_node_parameter,
    unknown_binding_node,
    unknown_binding_parameter,
    unknown_workflow_parameter,
    duplicate_workflow_parameter,
    parameter_source_conflict,
    invalid_parameter_value,
    required_parameter_missing,
    duplicate_resource,
    unknown_resource,
    unknown_binding_input,
    duplicate_binding_reference,
    input_source_conflict,
    incompatible_artifact_type,
    required_input_missing,
};

[[nodiscard]] std::string_view to_string(WorkflowBindingErrorCode code) noexcept;

class WorkflowBindingError final : public std::invalid_argument {
public:
    WorkflowBindingError(WorkflowBindingErrorCode code, std::string message);

    [[nodiscard]] WorkflowBindingErrorCode code() const noexcept;

private:
    WorkflowBindingErrorCode code_;
};

struct WorkflowParameterBinding final {
    std::string name;
    domain::PluginParameterValue value;
};

struct WorkflowArtifactResource final {
    std::string name;
    std::string resource_id;
    std::string artifact_type;
};

struct WorkflowNodeParameterReference final {
    domain::WorkflowNodeId node_id;
    std::string parameter_name;
    std::string workflow_parameter_name;
};

struct WorkflowNodeResourceReference final {
    domain::WorkflowNodeId node_id;
    std::string input_port;
    std::string resource_name;
};

struct WorkflowBindingRequest final {
    std::vector<WorkflowParameterBinding> parameters;
    std::vector<WorkflowArtifactResource> resources;
    std::vector<WorkflowNodeParameterReference> parameter_references;
    std::vector<WorkflowNodeResourceReference> resource_references;
};

enum class ResolvedWorkflowParameterSource {
    node_literal,
    workflow_parameter,
    module_default,
};

[[nodiscard]] std::string_view to_string(ResolvedWorkflowParameterSource source) noexcept;

struct ResolvedWorkflowParameter final {
    std::string name;
    domain::PluginParameterType type{domain::PluginParameterType::string};
    std::string value;
    ResolvedWorkflowParameterSource source{ResolvedWorkflowParameterSource::node_literal};
};

enum class ResolvedWorkflowInputSourceKind {
    workflow_resource,
    node_output,
};

[[nodiscard]] std::string_view to_string(ResolvedWorkflowInputSourceKind kind) noexcept;

struct ResolvedWorkflowInputBinding final {
    std::string port_name;
    std::string artifact_type;
    ResolvedWorkflowInputSourceKind source_kind{ResolvedWorkflowInputSourceKind::workflow_resource};
    std::string source_id;
    std::string source_port;
};

struct ResolvedWorkflowOutputBinding final {
    std::string port_name;
    std::string artifact_type;
};

struct ResolvedWorkflowNodeBinding final {
    domain::WorkflowNodeId node_id;
    std::string module_id;
    std::string plugin_version;
    std::vector<ResolvedWorkflowParameter> parameters;
    std::vector<ResolvedWorkflowInputBinding> inputs;
    std::vector<ResolvedWorkflowOutputBinding> outputs;
};

class WorkflowBindingPlan final {
public:
    WorkflowBindingPlan(
        domain::WorkflowId workflow_id,
        std::vector<ResolvedWorkflowNodeBinding> nodes
    );

    [[nodiscard]] const domain::WorkflowId& workflow_id() const noexcept;
    [[nodiscard]] const std::vector<ResolvedWorkflowNodeBinding>& nodes() const noexcept;

private:
    domain::WorkflowId workflow_id_;
    std::vector<ResolvedWorkflowNodeBinding> nodes_;
};

class WorkflowBindingResolver final {
public:
    [[nodiscard]] static WorkflowBindingPlan resolve(
        const domain::Workflow& workflow,
        const WorkflowBindingRequest& request,
        const IPluginRegistry& registry
    );
};

}  // namespace biocore::application
