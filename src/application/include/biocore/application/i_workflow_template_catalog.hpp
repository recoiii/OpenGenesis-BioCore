#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/domain/workflow_template.hpp"

namespace biocore::application {

struct RegisteredWorkflowTemplate final {
    std::string id;
    std::string version;
    std::string name;
    std::string description;
};

class IWorkflowTemplateCatalog {
public:
    virtual ~IWorkflowTemplateCatalog() = default;

    [[nodiscard]] virtual std::optional<domain::WorkflowTemplate> find(
        std::string_view template_id,
        std::string_view template_version
    ) const = 0;

    [[nodiscard]] virtual std::vector<RegisteredWorkflowTemplate> list() const = 0;
};

}  // namespace biocore::application
