#pragma once

#include <filesystem>

#include "biocore/domain/workflow_template.hpp"

namespace biocore::infrastructure {

class JsonWorkflowTemplateLoader final {
public:
    [[nodiscard]] domain::WorkflowTemplate load(
        const std::filesystem::path& path
    ) const;
};

}  // namespace biocore::infrastructure
