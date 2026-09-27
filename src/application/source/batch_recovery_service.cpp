#include "biocore/application/batch_recovery_service.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/batch_execution_materializer.hpp"
#include "biocore/application/i_batch_execution_store.hpp"
#include "biocore/application/i_batch_plan_store.hpp"
#include "biocore/application/i_execution_plan_store.hpp"
#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_input_file_storage.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_plugin_registry.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/i_workflow_checkpoint_artifact_verifier.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/workflow_resume_planner.hpp"
#include "biocore/domain/job_status.hpp"
#include "biocore/domain/workflow.hpp"
#include "biocore/domain/workflow_checkpoint.hpp"

namespace biocore::application {
namespace {

constexpr std::string_view batch_pipeline_id = "org.biocore.batch.workflow";
constexpr std::string_view batch_pipeline_version = "1.0.0";
constexpr std::uint32_t batch_node_max_attempts = 3U;

struct RecoveryContext final {
    BatchExecutionRecord execution;
    ApprovedBatchPlan plan;
    const ApprovedBatchSamplePlan* sample{nullptr};
    std::vector<BatchExecutionAttemptRecord> attempts;
    BatchExecutionAttemptRecord latest;
    domain::Job latest_job;
};

[[nodiscard]] const ApprovedBatchSamplePlan* find_sample(
    const ApprovedBatchPlan& plan,
    const std::string_view sample_id
) noexcept {
    const auto found = std::ranges::find_if(plan.samples, [sample_id](const auto& sample) {
        return sample.sample_id == sample_id;
    });
    return found == plan.samples.end() ? nullptr : &*found;
}

[[nodiscard]] const BatchPlanNodeSnapshot* find_node(
    const ApprovedBatchSamplePlan& sample,
    const std::string_view node_id
) noexcept {
    const auto found = std::ranges::find_if(sample.nodes, [node_id](const auto& node) {
        return node.node_id == node_id;
    });
    return found == sample.nodes.end() ? nullptr : &*found;
}

[[nodiscard]] domain::Workflow build_workflow(const ApprovedBatchSamplePlan& sample) {
    if (!sample.workflow_id.has_value()) {
        throw std::logic_error{"Frozen batch sample is missing workflow identity"};
    }
    std::vector<domain::WorkflowNode> nodes;
    std::vector<domain::WorkflowEdge> edges;
    nodes.reserve(sample.nodes.size());

    for (const auto& node : sample.nodes) {
        std::vector<domain::WorkflowInputDeclaration> inputs;
        std::vector<domain::WorkflowOutputDeclaration> outputs;
        domain::WorkflowParameters parameters;
        inputs.reserve(node.inputs.size());
        outputs.reserve(node.outputs.size());
        for (const auto& input : node.inputs) {
            inputs.emplace_back(input.port_name, input.artifact_type, true);
            if (input.source_kind == BatchPlanInputSourceKind::node_output) {
                edges.emplace_back(
                    domain::WorkflowNodeId{input.source_id},
                    input.source_port,
                    domain::WorkflowNodeId{node.node_id},
                    input.port_name
                );
            }
        }
        for (const auto& output : node.outputs) {
            outputs.emplace_back(output.port_name, output.artifact_type);
        }
        for (const auto& parameter : node.parameters) {
            parameters.emplace(parameter.name, parameter.value);
        }
        nodes.emplace_back(
            domain::WorkflowNodeId{node.node_id},
            node.node_id,
            node.module_id,
            node.plugin_version,
            std::move(inputs),
            std::move(outputs),
            std::move(parameters)
        );
    }

    return domain::Workflow{
        domain::Workflow::current_schema_version,
        domain::WorkflowId{*sample.workflow_id},
        "Frozen batch sample " + sample.sample_id,
        "Iteration 084 recovery view of an immutable approved batch sample plan.",
        std::move(nodes),
        std::move(edges),
    };
}

[[nodiscard]] std::optional<domain::WorkflowCheckpointArtifact> checkpoint_artifact(
    const GeneratedOutputArtifact& artifact,
    const BatchPlanOutputSnapshot& expected
) {
    if (artifact.provenance.output_port != expected.port_name ||
        artifact.provenance.file_type != expected.artifact_type ||
        artifact.file.file_type() != expected.artifact_type ||
        !artifact.file.relative_project_path().has_value() ||
        artifact.file.checksum_algorithm() != std::optional<std::string>{"sha256"} ||
        !artifact.file.checksum_value().has_value()) {
        return std::nullopt;
    }
    return domain::WorkflowCheckpointArtifact{
        .output_port = expected.port_name,
        .relative_project_path = *artifact.file.relative_project_path(),
        .size_bytes = artifact.file.size_bytes(),
        .sha256 = *artifact.file.checksum_value(),
    };
}

[[nodiscard]] domain::WorkflowCheckpointFailure checkpoint_failure(
    const domain::Job& job,
    const std::string_view fallback
) {
    if (job.failure().has_value()) {
        return domain::WorkflowCheckpointFailure{
            .message = std::string{job.failure()->message()},
            .exit_code = job.failure()->exit_code(),
        };
    }
    return domain::WorkflowCheckpointFailure{
        .message = std::string{fallback},
        .exit_code = std::nullopt,
    };
}

[[nodiscard]] domain::WorkflowCheckpointManifest reconstruct_checkpoint(
    const ApprovedBatchSamplePlan& sample,
    const std::vector<BatchExecutionAttemptRecord>& attempts,
    JobService& jobs,
    IManagedFileRepository& managed_files
) {
    const domain::Workflow workflow = build_workflow(sample);
    std::map<std::string, domain::WorkflowNodeCheckpoint, std::less<>> checkpoint_by_id;
    for (const auto& node : sample.nodes) {
        checkpoint_by_id.emplace(node.node_id, domain::WorkflowNodeCheckpoint{
            .node_id = domain::WorkflowNodeId{node.node_id},
            .state = domain::WorkflowCheckpointNodeState::pending,
            .attempt_number = 0U,
            .max_attempts = batch_node_max_attempts,
            .outputs = {},
            .failure = std::nullopt,
        });
    }

    for (const auto& attempt : attempts) {
        const auto job = jobs.find_by_id(attempt.job_id);
        if (!job.has_value()) {
            throw std::runtime_error{"Batch attempt lineage references a missing job"};
        }
        if (attempt.execution_node_ids.empty()) continue;

        const auto generated = managed_files.list_generated_outputs(attempt.job_id);
        std::map<std::pair<std::string, std::string>, const GeneratedOutputArtifact*> outputs;
        for (const auto& artifact : generated) {
            outputs.emplace(
                std::pair<std::string, std::string>{
                    artifact.provenance.step_id, artifact.provenance.output_port
                },
                &artifact
            );
        }

        bool terminal_node_seen = false;
        for (std::size_t ordinal = 0U; ordinal < attempt.execution_node_ids.size(); ++ordinal) {
            const std::string& node_id = attempt.execution_node_ids[ordinal];
            const auto* frozen = find_node(sample, node_id);
            if (frozen == nullptr) {
                throw std::runtime_error{"Batch attempt references a node outside the frozen plan"};
            }
            auto& checkpoint = checkpoint_by_id.at(node_id);

            std::vector<domain::WorkflowCheckpointArtifact> durable_outputs;
            durable_outputs.reserve(frozen->outputs.size());
            bool outputs_complete = !frozen->outputs.empty();
            for (const auto& expected : frozen->outputs) {
                const auto found = outputs.find({node_id, expected.port_name});
                if (found == outputs.end()) {
                    outputs_complete = false;
                    continue;
                }
                const auto converted = checkpoint_artifact(*found->second, expected);
                if (!converted.has_value()) {
                    outputs_complete = false;
                    continue;
                }
                durable_outputs.push_back(*converted);
            }

            const double completion_threshold =
                static_cast<double>(ordinal + 1U) /
                static_cast<double>(attempt.execution_node_ids.size());
            const bool progress_complete = job->progress() + 1.0e-12 >= completion_threshold;
            const bool node_completed =
                job->status() == domain::JobStatus::completed ||
                outputs_complete || progress_complete;

            if (node_completed && !terminal_node_seen) {
                if (checkpoint.attempt_number <
                    domain::WorkflowCheckpointManifest::maximum_attempts) {
                    ++checkpoint.attempt_number;
                }
                checkpoint.state = domain::WorkflowCheckpointNodeState::completed;
                checkpoint.outputs = std::move(durable_outputs);
                checkpoint.failure.reset();
                continue;
            }

            if (!terminal_node_seen &&
                (job->status() == domain::JobStatus::failed ||
                 job->status() == domain::JobStatus::interrupted)) {
                terminal_node_seen = true;
                if (checkpoint.attempt_number <
                    domain::WorkflowCheckpointManifest::maximum_attempts) {
                    ++checkpoint.attempt_number;
                }
                checkpoint.state = job->status() == domain::JobStatus::failed
                    ? domain::WorkflowCheckpointNodeState::failed
                    : domain::WorkflowCheckpointNodeState::interrupted;
                checkpoint.outputs.clear();
                checkpoint.failure = checkpoint_failure(
                    *job,
                    job->status() == domain::JobStatus::failed
                        ? "Batch attempt failed before this node completed."
                        : "Batch attempt was interrupted before this node completed."
                );
            }
        }
    }

    std::vector<domain::WorkflowNodeCheckpoint> checkpoints;
    checkpoints.reserve(sample.nodes.size());
    for (const auto& node : workflow.nodes()) {
        checkpoints.push_back(checkpoint_by_id.at(std::string{node.id().value()}));
    }
    return domain::WorkflowCheckpointManifest{
        domain::WorkflowCheckpointManifest::current_schema_version,
        workflow.id(),
        std::move(checkpoints),
    };
}

[[nodiscard]] std::vector<BatchExecutionAttemptRecord> sample_attempts(
    std::vector<BatchExecutionAttemptRecord> attempts,
    const std::string_view sample_id
) {
    std::erase_if(attempts, [sample_id](const auto& attempt) {
        return attempt.sample_id != sample_id;
    });
    std::ranges::sort(attempts, {}, &BatchExecutionAttemptRecord::attempt_number);
    return attempts;
}

[[nodiscard]] RecoveryContext load_context(
    IBatchPlanStore& plans,
    IBatchExecutionStore& executions,
    JobService& jobs,
    const std::string_view plan_id,
    const std::string_view sample_id
) {
    const auto execution = executions.find(plan_id);
    if (!execution.has_value()) {
        throw std::invalid_argument{"Batch execution was not found"};
    }
    if (execution->cancellation_requested) {
        throw std::logic_error{"Cancelled batch executions cannot create recovery attempts"};
    }
    const auto plan = plans.find(plan_id);
    if (!plan.has_value()) {
        throw std::runtime_error{"Batch execution references a missing approved plan"};
    }
    const auto* sample = find_sample(*plan, sample_id);
    if (sample == nullptr || sample->disposition != BatchPlanSampleDisposition::included) {
        throw std::invalid_argument{"Included batch sample was not found"};
    }
    auto attempts = sample_attempts(executions.list_attempts(plan_id), sample_id);
    if (attempts.empty()) {
        throw std::runtime_error{"Batch sample has no persisted attempt lineage"};
    }
    const auto job = jobs.find_by_id(attempts.back().job_id);
    if (!job.has_value()) {
        throw std::runtime_error{"Latest batch attempt job is missing"};
    }
    const BatchExecutionAttemptRecord latest = attempts.back();
    RecoveryContext context{
        .execution = *execution,
        .plan = *plan,
        .sample = nullptr,
        .attempts = std::move(attempts),
        .latest = latest,
        .latest_job = *job,
    };
    context.sample = find_sample(context.plan, sample_id);
    if (context.sample == nullptr) {
        throw std::runtime_error{"Batch recovery context lost frozen sample"};
    }
    return context;
}

void validate_frozen_inputs(
    const ApprovedBatchSamplePlan& sample,
    const IPluginRegistry& plugins,
    IManagedFileRepository& managed_files,
    const IInputFileStorage& input_storage
) {
    static_cast<void>(materialize_batch_execution_plan(
        sample,
        "batch-recovery-validation-probe",
        0,
        plugins,
        managed_files,
        input_storage
    ));
}

[[nodiscard]] BatchCheckpointOutputPaths reusable_output_paths(
    const domain::WorkflowCheckpointManifest& manifest,
    const WorkflowResumePlan& plan
) {
    std::map<std::string, const domain::WorkflowNodeCheckpoint*, std::less<>> checkpoints;
    for (const auto& checkpoint : manifest.nodes()) {
        checkpoints.emplace(std::string{checkpoint.node_id.value()}, &checkpoint);
    }
    BatchCheckpointOutputPaths paths;
    for (const auto& action : plan.actions()) {
        if (action.action != WorkflowResumeActionKind::reuse_completed) continue;
        const auto* checkpoint = checkpoints.at(std::string{action.node_id.value()});
        for (const auto& output : checkpoint->outputs) {
            paths.emplace(
                std::pair<std::string, std::string>{
                    std::string{action.node_id.value()}, output.output_port
                },
                output.relative_project_path
            );
        }
    }
    return paths;
}

[[nodiscard]] std::vector<std::string> nodes_to_execute(const WorkflowResumePlan& plan) {
    std::vector<std::string> nodes;
    for (const auto& action : plan.actions()) {
        if (action.action == WorkflowResumeActionKind::execute ||
            action.action == WorkflowResumeActionKind::retry ||
            action.action == WorkflowResumeActionKind::reevaluate_condition) {
            nodes.emplace_back(action.node_id.value());
        } else if (action.action == WorkflowResumeActionKind::exhausted ||
                   action.action == WorkflowResumeActionKind::preserve_blocked) {
            throw std::logic_error{"Checkpoint cannot be resumed safely"};
        }
    }
    return nodes;
}

[[nodiscard]] BatchPreparedAttempt prepared_attempt(
    const RecoveryContext& context,
    const BatchAttemptMode mode,
    const std::string& job_id,
    const std::string& timestamp,
    const domain::JobPriority priority,
    std::optional<ExecutionPlan> plan,
    IExecutionPlanStore& execution_plans
) {
    const std::int64_t next_attempt = context.latest.attempt_number + 1;
    if (next_attempt > 64) {
        throw std::overflow_error{"Batch recovery attempt limit reached"};
    }

    if (plan.has_value()) {
        const std::string path = execution_plans.store(*plan);
        domain::Job job{
            job_id,
            context.execution.plan_id,
            std::string{batch_pipeline_id},
            std::string{batch_pipeline_version},
            domain::JobStatus::draft,
            priority,
            0.0,
            std::nullopt,
            timestamp,
            timestamp,
            std::nullopt,
            std::nullopt,
            0,
        };
        job.transition_to(domain::JobStatus::queued, 0.0, std::nullopt, timestamp);
        std::vector<std::string> node_ids;
        node_ids.reserve(plan->steps().size());
        for (const auto& step : plan->steps()) node_ids.push_back(step.id);
        return BatchPreparedAttempt{
            .attempt = BatchExecutionAttemptRecord{
                .plan_id = context.execution.plan_id,
                .sample_id = context.sample->sample_id,
                .attempt_number = next_attempt,
                .job_id = job_id,
                .parent_job_id = context.latest.job_id,
                .mode = mode,
                .created_at_utc = timestamp,
                .execution_node_ids = std::move(node_ids),
            },
            .job = std::move(job),
            .execution = PreparedJobExecution{
                .job_id = job_id,
                .attempt_number = 1,
                .launch_revision = 2,
                .pipeline_id = std::string{batch_pipeline_id},
                .pipeline_version = std::string{batch_pipeline_version},
                .execution_plan_path = path,
                .prepared_at_utc = timestamp,
            },
        };
    }

    domain::Job completed{
        job_id,
        context.execution.plan_id,
        std::string{batch_pipeline_id},
        std::string{batch_pipeline_version},
        domain::JobStatus::completed,
        priority,
        1.0,
        std::nullopt,
        timestamp,
        timestamp,
        timestamp,
        timestamp,
        1,
    };
    return BatchPreparedAttempt{
        .attempt = BatchExecutionAttemptRecord{
            .plan_id = context.execution.plan_id,
            .sample_id = context.sample->sample_id,
            .attempt_number = next_attempt,
            .job_id = job_id,
            .parent_job_id = context.latest.job_id,
            .mode = mode,
            .created_at_utc = timestamp,
            .execution_node_ids = {},
        },
        .job = std::move(completed),
        .execution = std::nullopt,
    };
}

}  // namespace

std::string_view to_string(const BatchRecoveryAction action) noexcept {
    switch (action) {
        case BatchRecoveryAction::none: return "none";
        case BatchRecoveryAction::resume: return "resume";
        case BatchRecoveryAction::retry: return "retry";
        case BatchRecoveryAction::blocked: return "blocked";
        case BatchRecoveryAction::exhausted: return "exhausted";
    }
    return "blocked";
}

BatchRecoveryService::BatchRecoveryService(
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
) noexcept
    : plans_{plans}, executions_{executions}, plugins_{plugins},
      managed_files_{managed_files}, input_storage_{input_storage},
      checkpoint_verifier_{checkpoint_verifier}, execution_plans_{execution_plans},
      id_generator_{id_generator}, clock_{clock}, jobs_{jobs} {}

BatchRecoveryInspection BatchRecoveryService::inspect(const std::string_view plan_id) {
    const auto execution = executions_.find(plan_id);
    if (!execution.has_value()) {
        throw std::invalid_argument{"Batch execution was not found"};
    }
    const auto plan = plans_.find(plan_id);
    if (!plan.has_value()) {
        throw std::runtime_error{"Batch execution references a missing approved plan"};
    }
    const auto all_attempts = executions_.list_attempts(plan_id);
    BatchRecoveryInspection inspection{.plan_id = std::string{plan_id}, .samples = {}};
    inspection.samples.reserve(execution->jobs.size());

    for (const auto& root : execution->jobs) {
        const auto* sample = find_sample(*plan, root.sample_id);
        if (sample == nullptr) {
            throw std::runtime_error{"Batch execution references a missing frozen sample"};
        }
        const auto attempts = sample_attempts(all_attempts, root.sample_id);
        if (attempts.empty()) {
            throw std::runtime_error{"Batch sample has no attempt lineage"};
        }
        const auto latest_job = jobs_.find_by_id(attempts.back().job_id);
        if (!latest_job.has_value()) {
            throw std::runtime_error{"Batch attempt lineage references a missing job"};
        }

        BatchSampleRecoveryDecision decision{
            .sample_id = root.sample_id,
            .job_id = attempts.back().job_id,
            .attempt_number = attempts.back().attempt_number,
            .action = BatchRecoveryAction::none,
            .reason = "No recovery action is required for the current job state.",
        };

        if (execution->cancellation_requested) {
            decision.reason = "Batch cancellation is already requested.";
        } else if (latest_job->status() == domain::JobStatus::failed) {
            try {
                validate_frozen_inputs(*sample, plugins_, managed_files_, input_storage_);
                decision.action = BatchRecoveryAction::retry;
                decision.reason = "Failed sample requires an explicit new full retry attempt.";
            } catch (const std::exception& error) {
                decision.action = BatchRecoveryAction::blocked;
                decision.reason = error.what();
            }
        } else if (latest_job->status() == domain::JobStatus::interrupted) {
            try {
                validate_frozen_inputs(*sample, plugins_, managed_files_, input_storage_);
                const domain::Workflow workflow = build_workflow(*sample);
                const auto checkpoint = reconstruct_checkpoint(
                    *sample, attempts, jobs_, managed_files_
                );
                const auto resume_plan = plan_workflow_resume(
                    workflow, checkpoint, checkpoint_verifier_
                );
                bool reusable = false;
                bool recompute = false;
                bool exhausted = false;
                for (const auto& action : resume_plan.actions()) {
                    reusable = reusable || action.action == WorkflowResumeActionKind::reuse_completed;
                    recompute = recompute || action.action == WorkflowResumeActionKind::execute ||
                        action.action == WorkflowResumeActionKind::retry ||
                        action.action == WorkflowResumeActionKind::reevaluate_condition;
                    exhausted = exhausted || action.action == WorkflowResumeActionKind::exhausted ||
                        action.action == WorkflowResumeActionKind::preserve_blocked;
                }
                if (exhausted) {
                    decision.action = BatchRecoveryAction::exhausted;
                    decision.reason = "Checkpoint retry limit or dependency contract prevents resume.";
                } else if (reusable || !recompute) {
                    decision.action = BatchRecoveryAction::resume;
                    decision.reason = reusable
                        ? "Verified checkpoint outputs allow partial resume without rerunning completed nodes."
                        : "Checkpoint is complete and can be sealed without rerunning work.";
                } else {
                    decision.action = BatchRecoveryAction::retry;
                    decision.reason = "No verified completed checkpoint node can be reused; full retry is required.";
                }
            } catch (const std::exception& error) {
                decision.action = BatchRecoveryAction::blocked;
                decision.reason = error.what();
            }
        }
        inspection.samples.push_back(std::move(decision));
    }
    return inspection;
}

std::vector<BatchRecoveryInspection> BatchRecoveryService::inspect_all() {
    std::vector<BatchRecoveryInspection> inspections;
    for (const auto& execution : executions_.list()) {
        inspections.push_back(inspect(execution.plan_id));
    }
    return inspections;
}

BatchExecutionAttemptRecord BatchRecoveryService::resume(
    const std::string_view plan_id,
    const std::string_view sample_id,
    const domain::JobPriority priority
) {
    RecoveryContext context = load_context(plans_, executions_, jobs_, plan_id, sample_id);
    if (context.latest_job.status() != domain::JobStatus::interrupted) {
        throw std::logic_error{"Only interrupted batch attempts can resume"};
    }
    validate_frozen_inputs(*context.sample, plugins_, managed_files_, input_storage_);
    const domain::Workflow workflow = build_workflow(*context.sample);
    const auto checkpoint = reconstruct_checkpoint(
        *context.sample, context.attempts, jobs_, managed_files_
    );
    const auto resume_plan = plan_workflow_resume(
        workflow, checkpoint, checkpoint_verifier_
    );
    const auto execution_nodes = nodes_to_execute(resume_plan);
    const bool has_reusable_checkpoint = std::ranges::any_of(
        resume_plan.actions(),
        [](const auto& action) {
            return action.action == WorkflowResumeActionKind::reuse_completed;
        }
    );
    if (!has_reusable_checkpoint && !execution_nodes.empty()) {
        throw std::logic_error{
            "Checkpoint has no verified completed work; full retry is required"
        };
    }
    const auto reusable_outputs = reusable_output_paths(checkpoint, resume_plan);

    for (int identifier_attempt = 0; identifier_attempt < maximum_identifier_attempts;
         ++identifier_attempt) {
        const std::string job_id = id_generator_.generate();
        const std::string timestamp = clock_.now_utc_iso8601();
        std::optional<ExecutionPlan> plan;
        if (!execution_nodes.empty()) {
            plan = materialize_batch_resume_execution_plan(
                *context.sample, job_id, 2, execution_nodes, reusable_outputs,
                plugins_, managed_files_, input_storage_
            );
        }
        BatchPreparedAttempt attempt = prepared_attempt(
            context, BatchAttemptMode::resume, job_id, timestamp, priority,
            std::move(plan), execution_plans_
        );
        const std::optional<std::string> snapshot_path = attempt.execution.has_value()
            ? std::optional<std::string>{attempt.execution->execution_plan_path}
            : std::nullopt;
        AddBatchAttemptResult result{};
        try {
            result = executions_.add_attempt(attempt);
        } catch (...) {
            if (snapshot_path.has_value()) execution_plans_.discard(*snapshot_path);
            throw;
        }
        if (result == AddBatchAttemptResult::created) return attempt.attempt;
        if (snapshot_path.has_value()) execution_plans_.discard(*snapshot_path);
        if (result == AddBatchAttemptResult::attempt_conflict) {
            throw std::runtime_error{"Batch attempt lineage changed concurrently"};
        }
    }
    throw std::runtime_error{"Unable to allocate a unique resume Job identifier"};
}

BatchExecutionAttemptRecord BatchRecoveryService::retry(
    const std::string_view plan_id,
    const std::string_view sample_id,
    const domain::JobPriority priority
) {
    RecoveryContext context = load_context(plans_, executions_, jobs_, plan_id, sample_id);
    if (context.latest_job.status() != domain::JobStatus::failed &&
        context.latest_job.status() != domain::JobStatus::interrupted) {
        throw std::logic_error{"Only failed or interrupted batch attempts can retry"};
    }
    validate_frozen_inputs(*context.sample, plugins_, managed_files_, input_storage_);

    for (int identifier_attempt = 0; identifier_attempt < maximum_identifier_attempts;
         ++identifier_attempt) {
        const std::string job_id = id_generator_.generate();
        const std::string timestamp = clock_.now_utc_iso8601();
        std::optional<ExecutionPlan> plan = materialize_batch_execution_plan(
            *context.sample, job_id, 2, plugins_, managed_files_, input_storage_
        );
        BatchPreparedAttempt attempt = prepared_attempt(
            context, BatchAttemptMode::retry, job_id, timestamp, priority,
            std::move(plan), execution_plans_
        );
        const std::string snapshot_path = attempt.execution->execution_plan_path;
        AddBatchAttemptResult result{};
        try {
            result = executions_.add_attempt(attempt);
        } catch (...) {
            execution_plans_.discard(snapshot_path);
            throw;
        }
        if (result == AddBatchAttemptResult::created) return attempt.attempt;
        execution_plans_.discard(snapshot_path);
        if (result == AddBatchAttemptResult::attempt_conflict) {
            throw std::runtime_error{"Batch attempt lineage changed concurrently"};
        }
    }
    throw std::runtime_error{"Unable to allocate a unique retry Job identifier"};
}

}  // namespace biocore::application
