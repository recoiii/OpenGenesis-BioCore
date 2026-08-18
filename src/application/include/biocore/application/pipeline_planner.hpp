#pragma once

#include <cstdint>
#include <string_view>

#include "biocore/application/execution_plan.hpp"
#include "biocore/application/i_plugin_registry.hpp"
#include "biocore/domain/pipeline_definition.hpp"

namespace biocore::application {

class PipelinePlanner final {
public:
    [[nodiscard]] static ExecutionPlan create_execution_plan(
        const domain::PipelineDefinition& definition,
        std::string_view job_id,
        std::int64_t job_revision,
        const IPluginRegistry& plugin_registry
    );
};

}  // namespace biocore::application
