#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "biocore/application/i_workflow_template_catalog.hpp"

namespace biocore::infrastructure {

struct WorkflowTemplateDiscoveryIssue final {
    std::string candidate_path;
    std::string message;
};

struct WorkflowTemplateDiscoveryReport final {
    std::size_t loaded_templates{0U};
    std::vector<WorkflowTemplateDiscoveryIssue> rejected;
};

class FilesystemWorkflowTemplateCatalog final
    : public application::IWorkflowTemplateCatalog {
public:
    explicit FilesystemWorkflowTemplateCatalog(
        std::filesystem::path template_root
    );

    [[nodiscard]] std::optional<domain::WorkflowTemplate> find(
        std::string_view template_id,
        std::string_view template_version
    ) const override;

    [[nodiscard]] std::vector<application::RegisteredWorkflowTemplate>
    list() const override;

    [[nodiscard]] const WorkflowTemplateDiscoveryReport& report() const noexcept;

private:
    std::filesystem::path template_root_;
    WorkflowTemplateDiscoveryReport report_;
    std::vector<domain::WorkflowTemplate> templates_;
};

}  // namespace biocore::infrastructure
