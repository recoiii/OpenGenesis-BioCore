#pragma once

#include <optional>
#include <string_view>

namespace biocore::domain {

enum class JobStatus {
    draft,
    queued,
    preparing,
    running,
    paused,
    cancelling,
    cancelled,
    completed,
    failed,
    interrupted
};

[[nodiscard]] constexpr bool is_terminal(const JobStatus status) noexcept {
    return status == JobStatus::cancelled || status == JobStatus::completed || status == JobStatus::failed;
}

[[nodiscard]] bool can_transition(JobStatus from, JobStatus to) noexcept;
[[nodiscard]] bool occupies_worker_slot(JobStatus status) noexcept;
[[nodiscard]] std::string_view to_string(JobStatus status) noexcept;
[[nodiscard]] std::optional<JobStatus> job_status_from_string(std::string_view value) noexcept;

}  // namespace biocore::domain
