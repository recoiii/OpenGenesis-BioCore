#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/batch_plan.hpp"
#include "biocore/application/execution_plan.hpp"

namespace biocore::application {

class IInputFileStorage;
class IManagedFileRepository;
class IPluginRegistry;

using BatchCheckpointOutputPaths =
    std::map<std::pair<std::string, std::string>, std::string>;

[[nodiscard]] ExecutionPlan materialize_batch_execution_plan(
    const ApprovedBatchSamplePlan& sample,
    std::string_view job_id,
    std::int64_t job_revision,
    const IPluginRegistry& plugins,
    IManagedFileRepository& managed_files,
    const IInputFileStorage& input_storage
);

[[nodiscard]] ExecutionPlan materialize_batch_resume_execution_plan(
    const ApprovedBatchSamplePlan& sample,
    std::string_view job_id,
    std::int64_t job_revision,
    const std::vector<std::string>& execution_node_ids,
    const BatchCheckpointOutputPaths& reusable_outputs,
    const IPluginRegistry& plugins,
    IManagedFileRepository& managed_files,
    const IInputFileStorage& input_storage
);

}  // namespace biocore::application
