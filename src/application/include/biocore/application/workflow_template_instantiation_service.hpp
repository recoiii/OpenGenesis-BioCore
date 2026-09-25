#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_workflow_template_catalog.hpp"
#include "biocore/domain/workflow.hpp"

namespace biocore::application {

struct WorkflowTemplateParameterOverride final {
    domain::WorkflowNodeId node_id;
    std::string parameter_name;
    std::string value;
};

struct InstantiateWorkflowTemplateRequest final {
    std::string template_id;
    std::string template_version;
    domain::WorkflowId workflow_id;
    std::optional<std::string> name;
    std::optional<std::string> description;
    std::vector<WorkflowTemplateParameterOverride> parameter_overrides;
};

struct InstantiatedWorkflowTemplate final {
    std::string template_id;
    std::string template_version;
    domain::Workflow workflow;
};

enum class WorkflowTemplateInstantiationErrorCode {
    template_not_found,
    duplicate_parameter_override,
    unknown_override_node,
};

[[nodiscard]] std::string_view to_string(
    WorkflowTemplateInstantiationErrorCode code
) noexcept;

class WorkflowTemplateInstantiationError final : public std::invalid_argument {
public:
    WorkflowTemplateInstantiationError(
        WorkflowTemplateInstantiationErrorCode code,
        std::string message
    );

    [[nodiscard]] WorkflowTemplateInstantiationErrorCode code() const noexcept;

private:
    WorkflowTemplateInstantiationErrorCode code_;
};

class WorkflowTemplateInstantiationService final {
public:
    explicit WorkflowTemplateInstantiationService(
        const IWorkflowTemplateCatalog& catalog
    ) noexcept;

    [[nodiscard]] InstantiatedWorkflowTemplate instantiate(
        const InstantiateWorkflowTemplateRequest& request
    ) const;

private:
    const IWorkflowTemplateCatalog& catalog_;
};

}  // namespace biocore::application
