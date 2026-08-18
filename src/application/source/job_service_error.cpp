#include "biocore/application/job_service_error.hpp"

#include <utility>

namespace biocore::application {

JobServiceError::JobServiceError(const JobServiceErrorCode code, std::string message)
    : std::runtime_error{std::move(message)}, code_{code} {}

JobServiceErrorCode JobServiceError::code() const noexcept {
    return code_;
}

}  // namespace biocore::application
