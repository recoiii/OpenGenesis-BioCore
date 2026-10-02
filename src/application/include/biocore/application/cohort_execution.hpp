#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace biocore::application {

enum class CohortExecutionState {
    queued,
    running,
    completed,
    failed,
    cancelled,
    interrupted
};

[[nodiscard]] std::string_view to_string(CohortExecutionState state) noexcept;
[[nodiscard]] std::optional<CohortExecutionState>
cohort_execution_state_from_string(std::string_view value) noexcept;
[[nodiscard]] bool is_terminal(CohortExecutionState state) noexcept;
[[nodiscard]] bool is_retryable(CohortExecutionState state) noexcept;

struct CohortExecutionAttempt final {
    std::string project_id;
    std::string analysis_id;
    std::int64_t attempt_number{1};
    std::string attempt_id;
    std::optional<std::string> parent_attempt_id;
    std::optional<std::string> job_id;
    std::string snapshot_digest;
    std::string idempotency_key;
    std::string payload_digest;
    CohortExecutionState state{CohortExecutionState::queued};
    bool cancellation_requested{false};
    std::string created_at_utc;
    std::string updated_at_utc;
    std::optional<std::string> failure_message;
    std::optional<std::string> result_manifest_file_id;
    std::optional<std::string> result_manifest_sha256;

    friend bool operator==(const CohortExecutionAttempt&, const CohortExecutionAttempt&) = default;
};

struct CohortExecutionHistory final {
    std::string project_id;
    std::string analysis_id;
    std::string snapshot_digest;
    std::vector<CohortExecutionAttempt> attempts;
};

enum class CohortExecutionReserveResult {
    created,
    replayed,
    idempotency_conflict,
    analysis_already_submitted,
    retry_conflict
};

}  // namespace biocore::application
