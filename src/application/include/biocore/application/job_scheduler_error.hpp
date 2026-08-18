#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace biocore::application {

enum class JobSchedulerErrorCode {
    tick_already_in_progress,
    launch_failure_recovery_failed
};

class JobSchedulerError final : public std::runtime_error {
public:
    JobSchedulerError(JobSchedulerErrorCode code, std::string job_id, std::string message);

    [[nodiscard]] JobSchedulerErrorCode code() const noexcept;
    [[nodiscard]] std::string_view job_id() const noexcept;

private:
    JobSchedulerErrorCode code_;
    std::string job_id_;
};

}  // namespace biocore::application
