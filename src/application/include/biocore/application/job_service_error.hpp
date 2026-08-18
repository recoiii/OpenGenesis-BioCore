#pragma once

#include <stdexcept>
#include <string>

namespace biocore::application {

enum class JobServiceErrorCode {
    identifier_generation_exhausted,
    job_not_found,
    concurrent_update
};

class JobServiceError final : public std::runtime_error {
public:
    JobServiceError(JobServiceErrorCode code, std::string message);

    [[nodiscard]] JobServiceErrorCode code() const noexcept;

private:
    JobServiceErrorCode code_;
};

}  // namespace biocore::application
