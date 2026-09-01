#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "biocore/domain/job.hpp"

namespace biocore::application {

struct PreparedJobExecution final {
    std::string job_id;
    std::int64_t attempt_number{1};
    std::int64_t launch_revision{0};
    std::string pipeline_id;
    std::string pipeline_version;
    std::string execution_plan_path;
    std::string prepared_at_utc;
};

class IPreparedJobStore {
public:
    virtual ~IPreparedJobStore() = default;

    // Atomically inserts the queued Job and its immutable execution-plan association.
    // Returns false only when the job identifier already exists.
    virtual bool add_prepared_job(
        const domain::Job& queued_job,
        const PreparedJobExecution& execution
    ) = 0;

    // Atomically re-queues one interrupted prepared job and advances only its launch revision.
    // Stores that do not support retry may retain the default false result. The production
    // project store overrides this operation; false is also used for optimistic-concurrency races.
    virtual bool retry_prepared_job(
        const domain::Job&,
        std::int64_t,
        const PreparedJobExecution&
    ) {
        return false;
    }

    [[nodiscard]] virtual std::optional<PreparedJobExecution> find_execution(
        std::string_view job_id
    ) = 0;
};

}  // namespace biocore::application
