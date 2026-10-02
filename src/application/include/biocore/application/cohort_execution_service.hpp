#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include "biocore/application/cohort_execution.hpp"
#include "biocore/domain/job_priority.hpp"

namespace biocore::application {

class CohortAnalysisSelectionService;
class ICohortAnalysisSnapshotStore;
class ICohortExecutionStore;
class IIdGenerator;
class IJobSubmitter;
class IUtcClock;
class JobService;
struct CohortAnalysisSnapshot;

enum class CohortExecutionErrorCode {
    analysis_not_found,
    snapshot_digest_mismatch,
    input_changed,
    idempotency_conflict,
    already_submitted,
    attempt_not_found,
    not_retryable,
    cancellation_conflict,
    handoff_failed,
    concurrent_update,
    completion_not_verified
};

class CohortExecutionError final : public std::runtime_error {
public:
    CohortExecutionError(CohortExecutionErrorCode code, std::string message);
    [[nodiscard]] CohortExecutionErrorCode code() const noexcept;
private:
    CohortExecutionErrorCode code_;
};

class ICohortExecutionInputVerifier {
public:
    virtual ~ICohortExecutionInputVerifier() = default;
    [[nodiscard]] virtual bool verify(const CohortAnalysisSnapshot& snapshot) = 0;
};

class CohortExecutionInputVerifier final : public ICohortExecutionInputVerifier {
public:
    explicit CohortExecutionInputVerifier(CohortAnalysisSelectionService& selections) noexcept;
    [[nodiscard]] bool verify(const CohortAnalysisSnapshot& snapshot) override;
private:
    CohortAnalysisSelectionService& selections_;
};

struct SubmitCohortExecutionRequest final {
    std::string project_id;
    std::string analysis_id;
    std::string expected_snapshot_digest;
    std::string idempotency_key;
    std::string payload_digest;
    domain::JobPriority priority{domain::JobPriority::normal};
};

struct RetryCohortExecutionRequest final {
    std::string project_id;
    std::string analysis_id;
    std::string expected_parent_attempt_id;
    std::string idempotency_key;
    std::string payload_digest;
    domain::JobPriority priority{domain::JobPriority::normal};
};

struct CompleteCohortExecutionRequest final {
    std::string project_id;
    std::string analysis_id;
    std::string attempt_id;
    std::string result_manifest_file_id;
    std::string result_manifest_sha256;
};

class CohortExecutionService final {
public:
    static constexpr std::string_view pipeline_id = "org.biocore.cohort-analysis";
    static constexpr std::string_view pipeline_version = "1.0.0";
    static constexpr int maximum_identifier_attempts = 8;

    CohortExecutionService(
        ICohortAnalysisSnapshotStore& snapshots,
        ICohortExecutionStore& executions,
        ICohortExecutionInputVerifier& input_verifier,
        IJobSubmitter& submitter,
        JobService& jobs,
        IIdGenerator& ids,
        IUtcClock& clock
    ) noexcept;

    [[nodiscard]] CohortExecutionAttempt submit(
        const SubmitCohortExecutionRequest& request
    );

    [[nodiscard]] CohortExecutionAttempt retry(
        const RetryCohortExecutionRequest& request
    );

    [[nodiscard]] CohortExecutionAttempt cancel(
        std::string_view project_id,
        std::string_view analysis_id,
        std::string_view attempt_id
    );

    [[nodiscard]] CohortExecutionHistory reconcile(
        std::string_view project_id,
        std::string_view analysis_id
    );

    [[nodiscard]] CohortExecutionAttempt complete(
        const CompleteCohortExecutionRequest& request
    );

private:
    [[nodiscard]] CohortExecutionAttempt dispatch_reserved(
        CohortExecutionAttempt attempt,
        domain::JobPriority priority
    );

    ICohortAnalysisSnapshotStore& snapshots_;
    ICohortExecutionStore& executions_;
    ICohortExecutionInputVerifier& input_verifier_;
    IJobSubmitter& submitter_;
    JobService& jobs_;
    IIdGenerator& ids_;
    IUtcClock& clock_;
};

}  // namespace biocore::application
