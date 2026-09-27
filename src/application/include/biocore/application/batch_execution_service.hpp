#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

#include "biocore/application/batch_execution.hpp"
#include "biocore/domain/job_priority.hpp"

namespace biocore::application {

class IBatchExecutionStore;
class IBatchPlanStore;
class IExecutionPlanStore;
class IIdGenerator;
class IInputFileStorage;
class IManagedFileRepository;
class IPluginRegistry;
class IUtcClock;
class JobScheduler;
class JobService;

class BatchExecutionService final {
public:
    static constexpr int maximum_identifier_attempts = 8;

    BatchExecutionService(
        IBatchPlanStore& plans,
        IBatchExecutionStore& executions,
        const IPluginRegistry& plugins,
        IManagedFileRepository& managed_files,
        const IInputFileStorage& input_storage,
        IExecutionPlanStore& execution_plans,
        IIdGenerator& id_generator,
        IUtcClock& clock,
        JobService& jobs,
        JobScheduler& scheduler
    ) noexcept;

    [[nodiscard]] BatchExecutionSnapshot submit(
        std::string_view plan_id,
        std::size_t maximum_concurrent_jobs,
        domain::JobPriority priority = domain::JobPriority::normal
    );

    [[nodiscard]] std::optional<BatchExecutionSnapshot> find(
        std::string_view plan_id
    );

    [[nodiscard]] BatchExecutionSnapshot cancel(
        std::string_view plan_id
    );

private:
    [[nodiscard]] BatchExecutionSnapshot snapshot(
        const BatchExecutionRecord& execution
    );

    IBatchPlanStore& plans_;
    IBatchExecutionStore& executions_;
    const IPluginRegistry& plugins_;
    IManagedFileRepository& managed_files_;
    const IInputFileStorage& input_storage_;
    IExecutionPlanStore& execution_plans_;
    IIdGenerator& id_generator_;
    IUtcClock& clock_;
    JobService& jobs_;
    JobScheduler& scheduler_;
};

}  // namespace biocore::application
