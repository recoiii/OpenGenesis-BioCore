#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "biocore/domain/workflow_template.hpp"

namespace biocore::pipeline_protocol {

inline constexpr std::size_t maximum_workflow_template_document_bytes =
    2U * 1024U * 1024U;

[[nodiscard]] domain::WorkflowTemplate parse_workflow_template_document(
    std::string_view json
);

[[nodiscard]] std::string serialize_workflow_template_document(
    const domain::WorkflowTemplate& value
);

}  // namespace biocore::pipeline_protocol
