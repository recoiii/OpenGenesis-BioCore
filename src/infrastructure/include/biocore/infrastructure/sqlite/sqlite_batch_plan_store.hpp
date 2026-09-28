#pragma once

#include "biocore/application/i_batch_plan_store.hpp"

namespace biocore::infrastructure::sqlite {

class SqliteConnection;

class SqliteBatchPlanStore final : public application::IBatchPlanStore {
public:
    explicit SqliteBatchPlanStore(SqliteConnection& connection) noexcept;

    bool add(const application::ApprovedBatchPlan& plan) override;

    [[nodiscard]] std::optional<application::ApprovedBatchPlan> find(
        std::string_view plan_id
    ) override;

    [[nodiscard]] std::vector<application::ApprovedBatchPlan> list() override;

private:
    SqliteConnection& connection_;
};

}  // namespace biocore::infrastructure::sqlite
