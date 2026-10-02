#include "biocore/application/cohort_execution_service.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "biocore/application/cohort_analysis_selection_service.hpp"
#include "biocore/application/cohort_analysis_snapshot.hpp"
#include "biocore/application/i_cohort_analysis_snapshot_store.hpp"
#include "biocore/application/i_cohort_execution_store.hpp"
#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_job_submitter.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/domain/job_status.hpp"

namespace biocore::application {
namespace {

[[noreturn]] void fail(const CohortExecutionErrorCode code, std::string message) {
    throw CohortExecutionError{code, std::move(message)};
}

[[nodiscard]] bool valid_sha256(const std::string_view value) noexcept {
    return value.size() == 64U &&
           std::ranges::all_of(value, [](const char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

void require_token(
    const std::string_view value,
    const std::string_view name,
    const std::size_t maximum
) {
    if (value.empty() || value.size() > maximum ||
        value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string{name} + " is invalid");
    }
}

void validate_submit_identity(
    const std::string_view project_id,
    const std::string_view analysis_id,
    const std::string_view snapshot_digest,
    const std::string_view idempotency_key,
    const std::string_view payload_digest
) {
    require_token(project_id, "Project id", 128U);
    require_token(analysis_id, "Analysis id", 128U);
    require_token(idempotency_key, "Idempotency key", 200U);
    if (!valid_sha256(snapshot_digest) || !valid_sha256(payload_digest)) {
        throw std::invalid_argument(
            "Snapshot and execution payload digests must be lowercase SHA-256 values"
        );
    }
}

[[nodiscard]] CohortAnalysisSelectionRequest selection_from_snapshot(
    const CohortAnalysisSnapshot& snapshot
) {
    CohortAnalysisSelectionRequest request{
        .project_id = snapshot.project_id,
        .cohort_id = snapshot.cohort_id,
        .cohort_revision = snapshot.cohort_revision,
        .reference = {
            .managed_file_id = snapshot.reference.managed_file_id,
            .assembly = snapshot.reference.assembly,
            .custom_assembly_id = snapshot.reference.custom_assembly_id,
            .normalization_contract_version =
                snapshot.reference.normalization_contract_version,
            .aliases = snapshot.reference.aliases,
        },
        .selections = {},
    };
    request.selections.reserve(snapshot.sources.size());
    for (const auto& source : snapshot.sources) {
        request.selections.push_back({
            .project_sample_id = source.project_sample_id,
            .plan_id = source.plan_id,
            .attempt_number = source.attempt_number,
            .job_id = source.job_id,
            .step_id = source.step_id,
            .output_port = source.output_port,
            .managed_file_id = source.managed_file_id,
            .expected_sha256 = source.sha256,
            .vcf_sample_name = source.vcf_sample_name,
        });
    }
    return request;
}

[[nodiscard]] std::vector<CohortPinnedVcfSource> canonical_sources(
    std::vector<CohortPinnedVcfSource> values
) {
    std::ranges::sort(values, [](const auto& left, const auto& right) {
        return std::tie(
            left.project_sample_id,
            left.managed_file_id,
            left.vcf_sample_name
        ) < std::tie(
            right.project_sample_id,
            right.managed_file_id,
            right.vcf_sample_name
        );
    });
    return values;
}

[[nodiscard]] CohortExecutionState state_for_job(const domain::JobStatus status) {
    switch (status) {
        case domain::JobStatus::draft:
        case domain::JobStatus::queued:
            return CohortExecutionState::queued;
        case domain::JobStatus::preparing:
        case domain::JobStatus::running:
        case domain::JobStatus::paused:
        case domain::JobStatus::cancelling:
            return CohortExecutionState::running;
        case domain::JobStatus::completed:
            return CohortExecutionState::completed;
        case domain::JobStatus::failed:
            return CohortExecutionState::failed;
        case domain::JobStatus::cancelled:
            return CohortExecutionState::cancelled;
        case domain::JobStatus::interrupted:
            return CohortExecutionState::interrupted;
    }
    return CohortExecutionState::interrupted;
}

[[nodiscard]] std::optional<std::string> failure_message_for_job(
    const domain::Job& job
) {
    if (!job.failure().has_value()) return std::nullopt;
    return std::string{job.failure()->message()};
}

}  // namespace

CohortExecutionError::CohortExecutionError(
    const CohortExecutionErrorCode code,
    std::string message
) : std::runtime_error{std::move(message)}, code_{code} {}

CohortExecutionErrorCode CohortExecutionError::code() const noexcept {
    return code_;
}

CohortExecutionInputVerifier::CohortExecutionInputVerifier(
    CohortAnalysisSelectionService& selections
) noexcept : selections_{selections} {}

bool CohortExecutionInputVerifier::verify(
    const CohortAnalysisSnapshot& snapshot
) {
    auto preview = selections_.preview(selection_from_snapshot(snapshot));
    if (!preview.ready || !preview.reference.has_value() ||
        *preview.reference != snapshot.reference) {
        return false;
    }
    return canonical_sources(std::move(preview.sources)) ==
           canonical_sources(snapshot.sources);
}

CohortExecutionService::CohortExecutionService(
    ICohortAnalysisSnapshotStore& snapshots,
    ICohortExecutionStore& executions,
    ICohortExecutionInputVerifier& input_verifier,
    IJobSubmitter& submitter,
    JobService& jobs,
    IIdGenerator& ids,
    IUtcClock& clock
) noexcept
    : snapshots_{snapshots},
      executions_{executions},
      input_verifier_{input_verifier},
      submitter_{submitter},
      jobs_{jobs},
      ids_{ids},
      clock_{clock} {}

CohortExecutionAttempt CohortExecutionService::dispatch_reserved(
    CohortExecutionAttempt attempt,
    const domain::JobPriority priority
) {
    try {
        auto job = submitter_.submit({
            .analysis_id = attempt.analysis_id,
            .pipeline_id = std::string{pipeline_id},
            .pipeline_version = std::string{pipeline_version},
            .priority = priority,
            .bindings = {},
        });
        if (!job.analysis_id().has_value() ||
            *job.analysis_id() != attempt.analysis_id ||
            !job.pipeline_id().has_value() ||
            *job.pipeline_id() != pipeline_id ||
            !job.pipeline_version().has_value() ||
            *job.pipeline_version() != pipeline_version ||
            job.status() != domain::JobStatus::queued) {
            throw std::runtime_error{
                "Cohort scheduler handoff returned an incompatible prepared Job"
            };
        }
        const auto now = clock_.now_utc_iso8601();
        if (!executions_.attach_job(
                attempt.project_id,
                attempt.analysis_id,
                attempt.attempt_id,
                job.id(),
                now)) {
            fail(
                CohortExecutionErrorCode::concurrent_update,
                "Reserved cohort attempt changed before scheduler handoff could be attached"
            );
        }
        attempt.job_id = std::string{job.id()};
        attempt.updated_at_utc = now;
        return attempt;
    } catch (const CohortExecutionError&) {
        throw;
    } catch (const std::exception& error) {
        const auto now = clock_.now_utc_iso8601();
        const std::string message =
            std::string{"Cohort scheduler handoff failed: "} + error.what();
        (void)executions_.update_runtime_state(
            attempt.project_id,
            attempt.analysis_id,
            attempt.attempt_id,
            CohortExecutionState::interrupted,
            message,
            now
        );
        fail(CohortExecutionErrorCode::handoff_failed, message);
    }
}

CohortExecutionAttempt CohortExecutionService::submit(
    const SubmitCohortExecutionRequest& request
) {
    validate_submit_identity(
        request.project_id,
        request.analysis_id,
        request.expected_snapshot_digest,
        request.idempotency_key,
        request.payload_digest
    );
    const auto snapshot = snapshots_.find(request.project_id, request.analysis_id);
    if (!snapshot.has_value()) {
        fail(CohortExecutionErrorCode::analysis_not_found,
             "Approved cohort analysis snapshot was not found");
    }
    if (snapshot->snapshot_digest != request.expected_snapshot_digest) {
        fail(CohortExecutionErrorCode::snapshot_digest_mismatch,
             "Cohort execution snapshot digest does not match the approved snapshot");
    }
    if (!input_verifier_.verify(*snapshot)) {
        fail(CohortExecutionErrorCode::input_changed,
             "Pinned cohort input bytes or source lineage changed before execution");
    }

    CohortExecutionAttempt attempt{
        .project_id = request.project_id,
        .analysis_id = request.analysis_id,
        .attempt_number = 1,
        .attempt_id = ids_.generate(),
        .parent_attempt_id = std::nullopt,
        .job_id = std::nullopt,
        .snapshot_digest = snapshot->snapshot_digest,
        .idempotency_key = request.idempotency_key,
        .payload_digest = request.payload_digest,
        .state = CohortExecutionState::queued,
        .cancellation_requested = false,
        .created_at_utc = clock_.now_utc_iso8601(),
        .updated_at_utc = {},
        .failure_message = std::nullopt,
        .result_manifest_file_id = std::nullopt,
        .result_manifest_sha256 = std::nullopt,
    };
    attempt.updated_at_utc = attempt.created_at_utc;

    const auto reservation = executions_.reserve_initial(attempt);
    if (reservation == CohortExecutionReserveResult::replayed) {
        const auto values = executions_.list_attempts(request.project_id, request.analysis_id);
        for (const auto& value : values) {
            if (value.idempotency_key == request.idempotency_key) return value;
        }
        fail(CohortExecutionErrorCode::concurrent_update,
             "Replayed cohort execution reservation disappeared");
    }
    if (reservation == CohortExecutionReserveResult::idempotency_conflict) {
        fail(CohortExecutionErrorCode::idempotency_conflict,
             "Cohort execution idempotency key was reused with a different payload");
    }
    if (reservation == CohortExecutionReserveResult::analysis_already_submitted) {
        fail(CohortExecutionErrorCode::already_submitted,
             "Cohort analysis already has an initial execution attempt");
    }
    if (reservation != CohortExecutionReserveResult::created) {
        fail(CohortExecutionErrorCode::concurrent_update,
             "Cohort initial execution reservation failed");
    }
    return dispatch_reserved(std::move(attempt), request.priority);
}

CohortExecutionHistory CohortExecutionService::reconcile(
    const std::string_view project_id,
    const std::string_view analysis_id
) {
    const auto snapshot = snapshots_.find(project_id, analysis_id);
    if (!snapshot.has_value()) {
        fail(CohortExecutionErrorCode::analysis_not_found,
             "Approved cohort analysis snapshot was not found");
    }

    auto attempts = executions_.list_attempts(project_id, analysis_id);
    for (auto& attempt : attempts) {
        if (attempt.state == CohortExecutionState::completed) continue;

        if (!attempt.job_id.has_value()) {
            if (!is_terminal(attempt.state)) {
                const std::string message =
                    "Reserved cohort attempt has no durable scheduler handoff after recovery";
                const auto now = clock_.now_utc_iso8601();
                (void)executions_.update_runtime_state(
                    project_id, analysis_id, attempt.attempt_id,
                    CohortExecutionState::interrupted, message, now
                );
                attempt.state = CohortExecutionState::interrupted;
                attempt.failure_message = message;
                attempt.updated_at_utc = now;
            }
            continue;
        }

        const auto job = jobs_.find_by_id(*attempt.job_id);
        if (!job.has_value()) {
            const std::string message =
                "Cohort attempt Job disappeared during recovery";
            const auto now = clock_.now_utc_iso8601();
            (void)executions_.update_runtime_state(
                project_id, analysis_id, attempt.attempt_id,
                CohortExecutionState::interrupted, message, now
            );
            attempt.state = CohortExecutionState::interrupted;
            attempt.failure_message = message;
            attempt.updated_at_utc = now;
            continue;
        }

        auto target = state_for_job(job->status());
        auto failure = failure_message_for_job(*job);
        if (target == CohortExecutionState::completed) {
            const std::string message =
                "Job reached completed before a verified cohort result manifest was committed";
            target = CohortExecutionState::interrupted;
            failure = message;
        }
        if (target == attempt.state &&
            (!failure.has_value() || failure == attempt.failure_message)) {
            continue;
        }

        const auto now = clock_.now_utc_iso8601();
        const std::optional<std::string_view> failure_view =
            failure.has_value()
                ? std::optional<std::string_view>{*failure}
                : std::nullopt;
        if (!executions_.update_runtime_state(
                project_id, analysis_id, attempt.attempt_id,
                target, failure_view, now)) {
            fail(CohortExecutionErrorCode::concurrent_update,
                 "Cohort execution changed during recovery reconciliation");
        }
        attempt.state = target;
        attempt.failure_message = failure;
        attempt.updated_at_utc = now;
    }

    attempts = executions_.list_attempts(project_id, analysis_id);
    return {
        .project_id = std::string{project_id},
        .analysis_id = std::string{analysis_id},
        .snapshot_digest = snapshot->snapshot_digest,
        .attempts = std::move(attempts),
    };
}

CohortExecutionAttempt CohortExecutionService::cancel(
    const std::string_view project_id,
    const std::string_view analysis_id,
    const std::string_view attempt_id
) {
    auto attempt = executions_.find_attempt(project_id, analysis_id, attempt_id);
    if (!attempt.has_value()) {
        fail(CohortExecutionErrorCode::attempt_not_found,
             "Cohort execution attempt was not found");
    }
    if (attempt->state == CohortExecutionState::completed) {
        fail(CohortExecutionErrorCode::cancellation_conflict,
             "Completed cohort execution cannot be cancelled");
    }
    if (attempt->state == CohortExecutionState::cancelled) return *attempt;

    const auto now = clock_.now_utc_iso8601();
    if (!executions_.request_cancellation(
            project_id, analysis_id, attempt_id, now)) {
        fail(CohortExecutionErrorCode::concurrent_update,
             "Cohort cancellation request could not be persisted");
    }
    attempt->cancellation_requested = true;
    attempt->updated_at_utc = now;

    if (attempt->job_id.has_value()) {
        auto job = jobs_.find_by_id(*attempt->job_id);
        if (job.has_value()) {
            if (job->status() == domain::JobStatus::queued ||
                job->status() == domain::JobStatus::preparing ||
                job->status() == domain::JobStatus::running ||
                job->status() == domain::JobStatus::paused) {
                (void)jobs_.transition(
                    job->id(), domain::JobStatus::cancelling,
                    job->progress(), job->active_step_id()
                );
            } else if (job->status() == domain::JobStatus::interrupted) {
                (void)jobs_.transition(
                    job->id(), domain::JobStatus::cancelled,
                    job->progress(), std::nullopt
                );
            }
        }
    }
    return *attempt;
}

CohortExecutionAttempt CohortExecutionService::retry(
    const RetryCohortExecutionRequest& request
) {
    require_token(request.expected_parent_attempt_id, "Parent attempt id", 128U);
    validate_submit_identity(
        request.project_id,
        request.analysis_id,
        "0000000000000000000000000000000000000000000000000000000000000000",
        request.idempotency_key,
        request.payload_digest
    );
    const auto snapshot = snapshots_.find(request.project_id, request.analysis_id);
    if (!snapshot.has_value()) {
        fail(CohortExecutionErrorCode::analysis_not_found,
             "Approved cohort analysis snapshot was not found");
    }

    (void)reconcile(request.project_id, request.analysis_id);
    const auto attempts = executions_.list_attempts(request.project_id, request.analysis_id);
    if (attempts.empty()) {
        fail(CohortExecutionErrorCode::attempt_not_found,
             "Cohort analysis has no execution attempt to retry");
    }
    const auto& parent = attempts.back();
    if (parent.attempt_id != request.expected_parent_attempt_id ||
        !is_retryable(parent.state)) {
        fail(CohortExecutionErrorCode::not_retryable,
             "Only the latest failed, interrupted or cancelled cohort attempt may retry");
    }
    if (!input_verifier_.verify(*snapshot)) {
        fail(CohortExecutionErrorCode::input_changed,
             "Pinned cohort input bytes changed; retry cannot reuse the frozen execution");
    }

    CohortExecutionAttempt child{
        .project_id = request.project_id,
        .analysis_id = request.analysis_id,
        .attempt_number = parent.attempt_number + 1,
        .attempt_id = ids_.generate(),
        .parent_attempt_id = parent.attempt_id,
        .job_id = std::nullopt,
        .snapshot_digest = snapshot->snapshot_digest,
        .idempotency_key = request.idempotency_key,
        .payload_digest = request.payload_digest,
        .state = CohortExecutionState::queued,
        .cancellation_requested = false,
        .created_at_utc = clock_.now_utc_iso8601(),
        .updated_at_utc = {},
        .failure_message = std::nullopt,
        .result_manifest_file_id = std::nullopt,
        .result_manifest_sha256 = std::nullopt,
    };
    child.updated_at_utc = child.created_at_utc;

    const auto reservation = executions_.reserve_retry(
        child, request.expected_parent_attempt_id
    );
    if (reservation == CohortExecutionReserveResult::replayed) {
        const auto values = executions_.list_attempts(request.project_id, request.analysis_id);
        for (const auto& value : values) {
            if (value.idempotency_key == request.idempotency_key) return value;
        }
        fail(CohortExecutionErrorCode::concurrent_update,
             "Replayed retry reservation disappeared");
    }
    if (reservation == CohortExecutionReserveResult::idempotency_conflict) {
        fail(CohortExecutionErrorCode::idempotency_conflict,
             "Retry idempotency key was reused with a different payload");
    }
    if (reservation != CohortExecutionReserveResult::created) {
        fail(CohortExecutionErrorCode::not_retryable,
             "Cohort retry parent changed or another retry already won the reservation");
    }
    return dispatch_reserved(std::move(child), request.priority);
}

CohortExecutionAttempt CohortExecutionService::complete(
    const CompleteCohortExecutionRequest& request
) {
    auto attempt = executions_.find_attempt(
        request.project_id, request.analysis_id, request.attempt_id
    );
    if (!attempt.has_value() || !attempt->job_id.has_value()) {
        fail(CohortExecutionErrorCode::attempt_not_found,
             "Cohort execution attempt or scheduler Job was not found");
    }
    if (!valid_sha256(request.result_manifest_sha256)) {
        throw std::invalid_argument("Result manifest digest must be a lowercase SHA-256 value");
    }
    require_token(request.result_manifest_file_id, "Result manifest file id", 128U);

    const auto job = jobs_.find_by_id(*attempt->job_id);
    if (!job.has_value() || job->status() != domain::JobStatus::completed ||
        job->progress() != 1.0) {
        fail(CohortExecutionErrorCode::completion_not_verified,
             "Cohort execution cannot complete before its Job reaches verified completion");
    }

    const auto now = clock_.now_utc_iso8601();
    if (!executions_.record_completion(
            request.project_id,
            request.analysis_id,
            request.attempt_id,
            request.result_manifest_file_id,
            request.result_manifest_sha256,
            now)) {
        fail(CohortExecutionErrorCode::concurrent_update,
             "Cohort result manifest could not be committed atomically");
    }
    attempt = executions_.find_attempt(
        request.project_id, request.analysis_id, request.attempt_id
    );
    if (!attempt.has_value()) {
        fail(CohortExecutionErrorCode::concurrent_update,
             "Completed cohort attempt disappeared after manifest commit");
    }
    return *attempt;
}

}  // namespace biocore::application
