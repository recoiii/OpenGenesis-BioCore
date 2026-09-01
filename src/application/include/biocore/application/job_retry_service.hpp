#pragma once

#include <string_view>

#include "biocore/application/i_prepared_job_store.hpp"
#include "biocore/domain/job.hpp"

namespace biocore::application {

class JobService;
class IUtcClock;

struct RetryJobResult final {
    domain::Job job;
    PreparedJobExecution execution;
};

class JobRetryService final {
public:
    JobRetryService(
        JobService& jobs,
        IPreparedJobStore& prepared_jobs,
        IUtcClock& clock
    ) noexcept;

    [[nodiscard]] RetryJobResult retry(std::string_view job_id);

private:
    JobService& jobs_;
    IPreparedJobStore& prepared_jobs_;
    IUtcClock& clock_;
};

}  // namespace biocore::application
