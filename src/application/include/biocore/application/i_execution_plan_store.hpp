#pragma once

#include <string>
#include <string_view>

#include "biocore/application/execution_plan.hpp"

namespace biocore::application {

class IExecutionPlanStore {
public:
    virtual ~IExecutionPlanStore() = default;

    // Returns a canonical UTF-8 path to an immutable snapshot readable by the worker.
    [[nodiscard]] virtual std::string store(const ExecutionPlan& plan) = 0;
    virtual void discard(std::string_view snapshot_path) = 0;
};

}  // namespace biocore::application
