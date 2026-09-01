#include "biocore/application/job_retry_service.hpp"

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>

#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/job_service_error.hpp"
#include "biocore/domain/job_status.hpp"

namespace biocore::application {

JobRetryService::JobRetryService(
    JobService& jobs,
    IPreparedJobStore& prepared_jobs,
    IUtcClock& clock
) noexcept
    : jobs_{jobs}, prepared_jobs_{prepared_jobs}, clock_{clock} {}

RetryJobResult JobRetryService::retry(const std::string_view job_id) {
    auto job = jobs_.find_by_id(job_id);
    if (!job.has_value()) {
        throw JobServiceError{JobServiceErrorCode::job_not_found, "Job was not found"};
    }
    if (job->status() != domain::JobStatus::interrupted) {
        throw JobServiceError{
            JobServiceErrorCode::job_not_retryable,
            "Only interrupted jobs may be retried without changing their immutable execution plan",
        };
    }

    const auto prepared = prepared_jobs_.find_execution(job_id);
    if (!prepared.has_value()) {
        throw JobServiceError{
            JobServiceErrorCode::prepared_execution_missing,
            "Interrupted job has no prepared execution-plan association",
        };
    }
    if (job->revision() >= std::numeric_limits<std::int64_t>::max() - 1) {
        throw std::overflow_error("Job revision cannot reserve a retry launch revision");
    }

    const std::int64_t expected_revision = job->revision();
    job->transition_to(
        domain::JobStatus::queued,
        0.0,
        std::nullopt,
        clock_.now_utc_iso8601()
    );

    PreparedJobExecution retry_execution = *prepared;
    retry_execution.attempt_number = job->attempt_number();
    retry_execution.launch_revision = job->revision() + 1;

    if (!prepared_jobs_.retry_prepared_job(*job, expected_revision, retry_execution)) {
        throw JobServiceError{
            JobServiceErrorCode::concurrent_update,
            "Job changed before its retry could be persisted",
        };
    }

    return RetryJobResult{.job = *job, .execution = std::move(retry_execution)};
}

}  // namespace biocore::application
