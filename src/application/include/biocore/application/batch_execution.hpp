#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_prepared_job_store.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/job_status.hpp"

namespace biocore::application {

enum class BatchExecutionState {
    active,
    attention,
    cancelling,
    cancelled,
    completed,
    failed,
    partial_failure
};

[[nodiscard]] std::string_view to_string(BatchExecutionState state) noexcept;

enum class BatchAttemptMode {
    initial,
    resume,
    retry
};

[[nodiscard]] std::string_view to_string(BatchAttemptMode mode) noexcept;
[[nodiscard]] std::optional<BatchAttemptMode> batch_attempt_mode_from_string(
    std::string_view value
) noexcept;

struct BatchExecutionJobLink final {
    std::string sample_id;
    std::size_t ordinal{0U};
    std::string job_id;

    friend bool operator==(const BatchExecutionJobLink&, const BatchExecutionJobLink&) = default;
};

struct BatchExecutionRecord final {
    std::string plan_id;
    std::size_t maximum_concurrent_jobs{1U};
    bool cancellation_requested{false};
    std::string submitted_at_utc;
    std::string updated_at_utc;
    std::vector<BatchExecutionJobLink> jobs;

    friend bool operator==(const BatchExecutionRecord&, const BatchExecutionRecord&) = default;
};

struct BatchPreparedJob final {
    BatchExecutionJobLink link;
    domain::Job job;
    PreparedJobExecution execution;
};

struct BatchExecutionAttemptRecord final {
    std::string plan_id;
    std::string sample_id;
    std::int64_t attempt_number{1};
    std::string job_id;
    std::optional<std::string> parent_job_id;
    BatchAttemptMode mode{BatchAttemptMode::initial};
    std::string created_at_utc;
    std::vector<std::string> execution_node_ids;

    friend bool operator==(
        const BatchExecutionAttemptRecord&,
        const BatchExecutionAttemptRecord&
    ) = default;
};

struct BatchPreparedAttempt final {
    BatchExecutionAttemptRecord attempt;
    domain::Job job;
    std::optional<PreparedJobExecution> execution;
};

enum class AddBatchExecutionResult {
    created,
    plan_already_submitted,
    job_identifier_conflict
};

enum class AddBatchAttemptResult {
    created,
    attempt_conflict,
    job_identifier_conflict
};

struct BatchExecutionSampleView final {
    std::string sample_id;
    std::string job_id;
    domain::JobStatus status{domain::JobStatus::draft};
    double progress{0.0};
    std::int64_t attempt_number{1};
    BatchAttemptMode attempt_mode{BatchAttemptMode::initial};
    std::optional<std::string> parent_job_id;
};

struct BatchExecutionSnapshot final {
    std::string plan_id;
    std::size_t maximum_concurrent_jobs{1U};
    bool cancellation_requested{false};
    BatchExecutionState state{BatchExecutionState::active};
    std::size_t completed_count{0U};
    std::size_t failed_count{0U};
    std::size_t cancelled_count{0U};
    std::size_t interrupted_count{0U};
    std::size_t active_count{0U};
    std::vector<BatchExecutionSampleView> samples;
};

}  // namespace biocore::application
