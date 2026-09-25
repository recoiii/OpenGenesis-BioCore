#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "biocore/domain/workflow.hpp"

namespace biocore::pipeline_protocol {

inline constexpr std::size_t maximum_workflow_document_bytes = 1024U * 1024U;

[[nodiscard]] biocore::domain::Workflow parse_workflow_document(std::string_view json);
[[nodiscard]] std::string serialize_workflow_document(
    const biocore::domain::Workflow& workflow
);

}  // namespace biocore::pipeline_protocol
