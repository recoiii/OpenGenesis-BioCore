#include "biocore/application/job_submission_service_error.hpp"

#include <utility>

namespace biocore::application {

JobSubmissionError::JobSubmissionError(
    const JobSubmissionErrorCode code,
    std::string message
) : std::runtime_error{std::move(message)}, code_{code} {}

JobSubmissionErrorCode JobSubmissionError::code() const noexcept { return code_; }

}  // namespace biocore::application
