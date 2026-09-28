#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "biocore/application/batch_plan.hpp"

namespace biocore::application {

class IBatchPlanStore {
public:
    virtual ~IBatchPlanStore() = default;

    // Returns false on an immutable plan-id conflict. No partial rows may remain.
    virtual bool add(const ApprovedBatchPlan& plan) = 0;

    [[nodiscard]] virtual std::optional<ApprovedBatchPlan> find(
        std::string_view plan_id
    ) = 0;

    [[nodiscard]] virtual std::vector<ApprovedBatchPlan> list() = 0;
};

}  // namespace biocore::application
