#include "biocore/application/job_scheduler_error.hpp"

#include <utility>

namespace biocore::application {

JobSchedulerError::JobSchedulerError(
    const JobSchedulerErrorCode code,
    std::string job_id,
    std::string message
)
    : std::runtime_error{std::move(message)}, code_{code}, job_id_{std::move(job_id)} {}

JobSchedulerErrorCode JobSchedulerError::code() const noexcept {
    return code_;
}

std::string_view JobSchedulerError::job_id() const noexcept {
    return job_id_;
}

}  // namespace biocore::application
