#pragma once

#include <string>
#include <string_view>

#include "biocore/pipeline_protocol/pipeline_document.hpp"

namespace biocore::pipeline_protocol {

[[nodiscard]] PipelineDefinitionDocument parse_pipeline_definition_document(
    std::string_view json
);
[[nodiscard]] std::string serialize_pipeline_definition_document(
    const PipelineDefinitionDocument& document
);

[[nodiscard]] ExecutionPlanDocument parse_execution_plan_document(std::string_view json);
[[nodiscard]] std::string serialize_execution_plan_document(
    const ExecutionPlanDocument& document
);

}  // namespace biocore::pipeline_protocol
