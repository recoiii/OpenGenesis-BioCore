#pragma once

#include "biocore/application/i_batch_execution_store.hpp"

namespace biocore::infrastructure::sqlite {

class SqliteConnection;

class SqliteBatchExecutionStore final
    : public application::IBatchExecutionStore {
public:
    explicit SqliteBatchExecutionStore(SqliteConnection& connection) noexcept;

    application::AddBatchExecutionResult add(
        const application::BatchExecutionRecord& execution,
        std::span<const application::BatchPreparedJob> jobs
    ) override;

    [[nodiscard]] std::optional<application::BatchExecutionRecord> find(
        std::string_view plan_id
    ) override;

    bool request_cancellation(
        std::string_view plan_id,
        std::string_view updated_at_utc
    ) override;

    [[nodiscard]] std::optional<application::BatchSchedulingQuota> quota_for_job(
        std::string_view job_id
    ) override;

private:
    SqliteConnection& connection_;
};

}  // namespace biocore::infrastructure::sqlite
