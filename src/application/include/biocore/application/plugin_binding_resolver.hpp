#pragma once

#include "biocore/application/execution_plan.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/pipeline_bindings.hpp"

namespace biocore::application {

class PluginBindingResolver final {
public:
    [[nodiscard]] static ExecutionPlan resolve(
        const ExecutionPlan& plan,
        const PipelineRunBindings& bindings,
        IManagedFileRepository& managed_files
    );
};

}  // namespace biocore::application
