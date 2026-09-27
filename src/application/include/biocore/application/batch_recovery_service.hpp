#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/batch_execution.hpp"
#include "biocore/domain/job_priority.hpp"

namespace biocore::application {

class IBatchExecutionStore;
class IBatchPlanStore;
class IExecutionPlanStore;
class IIdGenerator;
class IInputFileStorage;
class IManagedFileRepository;
class IPluginRegistry;
class IUtcClock;
class IWorkflowCheckpointArtifactVerifier;
class JobService;

enum class BatchRecoveryAction {
    none,
    resume,
    retry,
    blocked,
    exhausted
};

[[nodiscard]] std::string_view to_string(BatchRecoveryAction action) noexcept;

struct BatchSampleRecoveryDecision final {
    std::string sample_id;
    std::string job_id;
    std::int64_t attempt_number{1};
    BatchRecoveryAction action{BatchRecoveryAction::none};
    std::string reason;
};

struct BatchRecoveryInspection final {
    std::string plan_id;
    std::vector<BatchSampleRecoveryDecision> samples;
};

class BatchRecoveryService final {
public:
    static constexpr int maximum_identifier_attempts = 8;

    BatchRecoveryService(
        IBatchPlanStore& plans,
        IBatchExecutionStore& executions,
        const IPluginRegistry& plugins,
        IManagedFileRepository& managed_files,
        const IInputFileStorage& input_storage,
        const IWorkflowCheckpointArtifactVerifier& checkpoint_verifier,
        IExecutionPlanStore& execution_plans,
        IIdGenerator& id_generator,
        IUtcClock& clock,
        JobService& jobs
    ) noexcept;

    [[nodiscard]] BatchRecoveryInspection inspect(std::string_view plan_id);
    [[nodiscard]] std::vector<BatchRecoveryInspection> inspect_all();

    [[nodiscard]] BatchExecutionAttemptRecord resume(
        std::string_view plan_id,
        std::string_view sample_id,
        domain::JobPriority priority = domain::JobPriority::normal
    );

    [[nodiscard]] BatchExecutionAttemptRecord retry(
        std::string_view plan_id,
        std::string_view sample_id,
        domain::JobPriority priority = domain::JobPriority::normal
    );

private:
    IBatchPlanStore& plans_;
    IBatchExecutionStore& executions_;
    const IPluginRegistry& plugins_;
    IManagedFileRepository& managed_files_;
    const IInputFileStorage& input_storage_;
    const IWorkflowCheckpointArtifactVerifier& checkpoint_verifier_;
    IExecutionPlanStore& execution_plans_;
    IIdGenerator& id_generator_;
    IUtcClock& clock_;
    JobService& jobs_;
};

}  // namespace biocore::application
