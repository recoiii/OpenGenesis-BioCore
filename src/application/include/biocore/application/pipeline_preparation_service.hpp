#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "biocore/application/execution_plan.hpp"
#include "biocore/application/i_execution_plan_store.hpp"
#include "biocore/application/i_plugin_registry.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/pipeline_bindings.hpp"
#include "biocore/domain/pipeline_definition.hpp"

namespace biocore::application {

struct PreparedExecutionPlan final {
    ExecutionPlan plan;
    std::string snapshot_path;
};

class PipelinePreparationService final {
public:
    PipelinePreparationService(
        IExecutionPlanStore& store,
        const IPluginRegistry& plugin_registry
    ) noexcept;

    PipelinePreparationService(
        IExecutionPlanStore& store,
        const IPluginRegistry& plugin_registry,
        IManagedFileRepository& managed_files
    ) noexcept;

    [[nodiscard]] PreparedExecutionPlan prepare(
        const domain::PipelineDefinition& definition,
        std::string_view job_id,
        std::int64_t job_revision
    );

    [[nodiscard]] PreparedExecutionPlan prepare(
        const domain::PipelineDefinition& definition,
        std::string_view job_id,
        std::int64_t job_revision,
        const PipelineRunBindings& bindings
    );

private:
    IExecutionPlanStore& store_;
    const IPluginRegistry& plugin_registry_;
    IManagedFileRepository* managed_files_{nullptr};
};

}  // namespace biocore::application
