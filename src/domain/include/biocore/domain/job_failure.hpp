#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace biocore::domain {

enum class JobFailureKind {
    worker_reported_failure,
    process_exit_without_terminal,
    heartbeat_timeout,
    startup_recovery,
    unspecified_terminal_failure,
    legacy_terminal_state
};

[[nodiscard]] std::string_view to_string(JobFailureKind kind) noexcept;
[[nodiscard]] std::optional<JobFailureKind> job_failure_kind_from_string(
    std::string_view value
) noexcept;

class JobFailure final {
public:
    static constexpr std::size_t maximum_message_length = 16U * 1024U;
    static constexpr std::size_t maximum_timestamp_length = 200U;

    JobFailure(
        JobFailureKind kind,
        std::string message,
        std::optional<std::int64_t> exit_code,
        std::optional<std::string> worker_timestamp_utc,
        std::string recorded_at_utc
    );

    [[nodiscard]] JobFailureKind kind() const noexcept;
    [[nodiscard]] std::string_view message() const noexcept;
    [[nodiscard]] const std::optional<std::int64_t>& exit_code() const noexcept;
    [[nodiscard]] const std::optional<std::string>& worker_timestamp_utc() const noexcept;
    [[nodiscard]] std::string_view recorded_at_utc() const noexcept;

private:
    JobFailureKind kind_;
    std::string message_;
    std::optional<std::int64_t> exit_code_;
    std::optional<std::string> worker_timestamp_utc_;
    std::string recorded_at_utc_;
};

}  // namespace biocore::domain
