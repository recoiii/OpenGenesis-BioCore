#include "biocore/domain/job_failure.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace biocore::domain {
namespace {

[[nodiscard]] bool is_blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
               return std::isspace(static_cast<unsigned char>(character)) != 0;
           });
}

void require_text(
    const std::string_view value,
    const std::string_view field,
    const std::size_t maximum_length
) {
    if (is_blank(value)) {
        throw std::invalid_argument(std::string{field} + " must not be blank");
    }
    if (value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string{field} + " must not contain NUL characters");
    }
    if (value.size() > maximum_length) {
        throw std::invalid_argument(std::string{field} + " exceeds the maximum length");
    }
}

}  // namespace

std::string_view to_string(const JobFailureKind kind) noexcept {
    switch (kind) {
        case JobFailureKind::worker_reported_failure: return "worker_reported_failure";
        case JobFailureKind::process_exit_without_terminal: return "process_exit_without_terminal";
        case JobFailureKind::heartbeat_timeout: return "heartbeat_timeout";
        case JobFailureKind::startup_recovery: return "startup_recovery";
        case JobFailureKind::unspecified_terminal_failure: return "unspecified_terminal_failure";
        case JobFailureKind::legacy_terminal_state: return "legacy_terminal_state";
    }
    return "unknown";
}

std::optional<JobFailureKind> job_failure_kind_from_string(const std::string_view value) noexcept {
    if (value == "worker_reported_failure") return JobFailureKind::worker_reported_failure;
    if (value == "process_exit_without_terminal") return JobFailureKind::process_exit_without_terminal;
    if (value == "heartbeat_timeout") return JobFailureKind::heartbeat_timeout;
    if (value == "startup_recovery") return JobFailureKind::startup_recovery;
    if (value == "unspecified_terminal_failure") return JobFailureKind::unspecified_terminal_failure;
    if (value == "legacy_terminal_state") return JobFailureKind::legacy_terminal_state;
    return std::nullopt;
}

JobFailure::JobFailure(
    const JobFailureKind kind,
    std::string message,
    std::optional<std::int64_t> exit_code,
    std::optional<std::string> worker_timestamp_utc,
    std::string recorded_at_utc
)
    : kind_{kind},
      message_{std::move(message)},
      exit_code_{exit_code},
      worker_timestamp_utc_{std::move(worker_timestamp_utc)},
      recorded_at_utc_{std::move(recorded_at_utc)} {
    require_text(message_, "Job failure message", maximum_message_length);
    require_text(recorded_at_utc_, "Job failure recorded timestamp", maximum_timestamp_length);
    if (worker_timestamp_utc_.has_value()) {
        require_text(
            *worker_timestamp_utc_, "Job failure worker timestamp", maximum_timestamp_length
        );
    }
    if (exit_code_.has_value() && *exit_code_ < 0) {
        throw std::invalid_argument("Job failure exit code must not be negative");
    }
    if (kind_ == JobFailureKind::worker_reported_failure) {
        if (!exit_code_.has_value() || *exit_code_ == 0 || !worker_timestamp_utc_.has_value()) {
            throw std::invalid_argument(
                "Worker-reported failure requires a non-zero exit code and worker timestamp"
            );
        }
    } else if (kind_ == JobFailureKind::process_exit_without_terminal) {
        if (!exit_code_.has_value()) {
            throw std::invalid_argument(
                "Process-exit failure requires a captured process exit code"
            );
        }
        if (worker_timestamp_utc_.has_value()) {
            throw std::invalid_argument(
                "Process-exit failure must not claim a worker lifecycle timestamp"
            );
        }
    } else if (exit_code_.has_value() || worker_timestamp_utc_.has_value()) {
        throw std::invalid_argument(
            "This job failure kind must not contain worker exit or timestamp evidence"
        );
    }
}

JobFailureKind JobFailure::kind() const noexcept { return kind_; }
std::string_view JobFailure::message() const noexcept { return message_; }
const std::optional<std::int64_t>& JobFailure::exit_code() const noexcept { return exit_code_; }
const std::optional<std::string>& JobFailure::worker_timestamp_utc() const noexcept {
    return worker_timestamp_utc_;
}
std::string_view JobFailure::recorded_at_utc() const noexcept { return recorded_at_utc_; }

}  // namespace biocore::domain
