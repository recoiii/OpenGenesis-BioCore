#pragma once

#include <optional>
#include <span>
#include <string_view>

#include "biocore/application/batch_execution.hpp"
#include "biocore/application/i_batch_scheduling_policy.hpp"

namespace biocore::application {

class IBatchExecutionStore : public IBatchSchedulingPolicy {
public:
    ~IBatchExecutionStore() override = default;

    virtual AddBatchExecutionResult add(
        const BatchExecutionRecord& execution,
        std::span<const BatchPreparedJob> jobs
    ) = 0;

    [[nodiscard]] virtual std::optional<BatchExecutionRecord> find(
        std::string_view plan_id
    ) = 0;

    virtual bool request_cancellation(
        std::string_view plan_id,
        std::string_view updated_at_utc
    ) = 0;
};

}  // namespace biocore::application
