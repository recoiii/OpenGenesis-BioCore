#include "biocore/application/batch_execution_service.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/execution_plan.hpp"
#include "biocore/application/i_batch_execution_store.hpp"
#include "biocore/application/i_batch_plan_store.hpp"
#include "biocore/application/i_execution_plan_store.hpp"
#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_input_file_storage.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_plugin_registry.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_scheduler.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/job_service_error.hpp"
#include "biocore/domain/job_status.hpp"
#include "biocore/domain/plugin_io_contract.hpp"

namespace biocore::application {
namespace {

constexpr std::string_view batch_pipeline_id = "org.biocore.batch.workflow";
constexpr std::string_view batch_pipeline_version = "1.0.0";

[[nodiscard]] std::string output_relative_path(
    const std::string_view job_id,
    const std::string_view node_id,
    const std::string_view port_name
) {
    return "outputs/" + std::string{job_id} + "--" + std::string{node_id} + "--" +
           std::string{port_name} + ".out";
}

[[nodiscard]] const domain::PluginParameterDefinition* find_parameter(
    const ResolvedPluginModule& module,
    const std::string_view name
) {
    const auto it = std::ranges::find_if(module.parameters, [name](const auto& value) {
        return value.name() == name;
    });
    return it == module.parameters.end() ? nullptr : &*it;
}

[[nodiscard]] const domain::PluginInputPortDefinition* find_input(
    const ResolvedPluginModule& module,
    const std::string_view name
) {
    const auto it = std::ranges::find_if(module.inputs, [name](const auto& value) {
        return value.name() == name;
    });
    return it == module.inputs.end() ? nullptr : &*it;
}

[[nodiscard]] const domain::PluginOutputPortDefinition* find_output(
    const ResolvedPluginModule& module,
    const std::string_view name
) {
    const auto it = std::ranges::find_if(module.outputs, [name](const auto& value) {
        return value.name() == name;
    });
    return it == module.outputs.end() ? nullptr : &*it;
}

[[nodiscard]] bool exact_file_identity(
    const domain::ManagedFile& file,
    const BatchPlanManagedFileSnapshot& expected
) {
    return file.id() == expected.file_id &&
           file.file_type() == expected.file_type &&
           file.size_bytes() == expected.size_bytes &&
           file.checksum_algorithm() == std::optional<std::string>{"sha256"} &&
           file.checksum_value() == std::optional<std::string>{expected.sha256} &&
           file.relative_project_path().has_value();
}

[[nodiscard]] ExecutionPlan materialize_execution_plan(
    const ApprovedBatchSamplePlan& sample,
    const std::string_view job_id,
    const std::int64_t job_revision,
    const IPluginRegistry& plugins,
    IManagedFileRepository& managed_files,
    const IInputFileStorage& input_storage
) {
    if (sample.disposition != BatchPlanSampleDisposition::included ||
        !sample.workflow_id.has_value() || sample.nodes.empty()) {
        throw std::invalid_argument{"Batch execution requires an included sample run plan"};
    }

    std::set<std::string, std::less<>> node_ids;
    std::map<std::pair<std::string, std::string>, ExecutionOutputBinding> outputs;
    std::vector<ExecutionPlanStep> steps;
    steps.reserve(sample.nodes.size());

    const double normalized_weight =
        1.0 / static_cast<double>(sample.nodes.size());

    for (const BatchPlanNodeSnapshot& node : sample.nodes) {
        if (!node_ids.emplace(node.node_id).second) {
            throw std::logic_error{"Approved batch plan contains duplicate node identities"};
        }

        const auto resolved = plugins.find_module(node.module_id);
        if (!resolved.has_value() ||
            resolved->module_id != node.module_id ||
            resolved->plugin_version != node.plugin_version) {
            throw std::runtime_error{
                "Approved batch node plugin identity is no longer available"
            };
        }

        ExecutionPlanStep step{
            .id = node.node_id,
            .module_id = node.module_id,
            .plugin_id = resolved->plugin_id,
            .plugin_version = resolved->plugin_version,
            .plugin_manifest_version = resolved->plugin_manifest_version,
            .plugin_api_version = resolved->plugin_api_version,
            .module_type = resolved->module_type,
            .plugin_root_path = resolved->plugin_root_path,
            .executable_path = resolved->executable_path,
            .depends_on = {},
            .normalized_weight = normalized_weight,
            .parameter_definitions = resolved->parameters,
            .input_definitions = resolved->inputs,
            .output_definitions = resolved->outputs,
            .parameters = {},
            .inputs = {},
            .outputs = {},
        };

        std::set<std::string, std::less<>> parameter_names;
        for (const BatchPlanParameterSnapshot& parameter : node.parameters) {
            const auto* definition = find_parameter(*resolved, parameter.name);
            if (definition == nullptr || definition->type() != parameter.type ||
                !parameter_names.emplace(parameter.name).second) {
                throw std::runtime_error{
                    "Approved batch parameter snapshot no longer matches the plugin contract"
                };
            }
            const auto value = domain::plugin_parameter_value_from_string(
                parameter.value, parameter.type
            );
            definition->validate_value(value);
            step.parameters.push_back(ExecutionParameterBinding{
                .name = parameter.name,
                .type = parameter.type,
                .value = parameter.value,
            });
        }
        for (const auto& definition : resolved->parameters) {
            if (definition.required() &&
                !parameter_names.contains(std::string{definition.name()})) {
                throw std::runtime_error{
                    "Approved batch snapshot is missing a required plugin parameter"
                };
            }
        }

        std::set<std::string, std::less<>> input_names;
        std::set<std::string, std::less<>> dependencies;
        for (const BatchPlanInputSnapshot& input : node.inputs) {
            const auto* definition = find_input(*resolved, input.port_name);
            if (definition == nullptr ||
                !definition->accepts_file_type(input.artifact_type) ||
                !input_names.emplace(input.port_name).second) {
                throw std::runtime_error{
                    "Approved batch input snapshot no longer matches the plugin contract"
                };
            }

            if (input.source_kind == BatchPlanInputSourceKind::managed_file) {
                if (!input.managed_file.has_value() ||
                    input.managed_file->file_id != input.source_id) {
                    throw std::logic_error{
                        "Approved batch managed input snapshot is internally inconsistent"
                    };
                }
                const auto current = managed_files.find_by_id(input.source_id);
                if (!current.has_value() ||
                    !exact_file_identity(*current, *input.managed_file)) {
                    throw std::runtime_error{
                        "Approved batch input identity changed before submission"
                    };
                }
                const auto integrity = input_storage.verify_managed_file(*current);
                if (integrity.status != ManagedFileIntegrityStatus::verified) {
                    throw std::runtime_error{
                        "Approved batch input is no longer integrity-verified"
                    };
                }
                step.inputs.push_back(ExecutionInputBinding{
                    .port_name = input.port_name,
                    .source_kind = ExecutionInputSourceKind::managed_file,
                    .source_id = input.source_id,
                    .file_type = input.artifact_type,
                    .relative_project_path = *current->relative_project_path(),
                });
            } else {
                if (input.managed_file.has_value() || input.source_port.empty()) {
                    throw std::logic_error{
                        "Approved batch node-output snapshot is internally inconsistent"
                    };
                }
                const auto output = outputs.find({input.source_id, input.source_port});
                if (output == outputs.end() ||
                    output->second.file_type != input.artifact_type) {
                    throw std::runtime_error{
                        "Approved batch dependency output is unavailable or incompatible"
                    };
                }
                dependencies.emplace(input.source_id);
                step.inputs.push_back(ExecutionInputBinding{
                    .port_name = input.port_name,
                    .source_kind = ExecutionInputSourceKind::step_output,
                    .source_id = input.source_id + "." + input.source_port,
                    .file_type = input.artifact_type,
                    .relative_project_path = output->second.relative_project_path,
                });
            }
        }
        for (const auto& definition : resolved->inputs) {
            if (definition.required() &&
                !input_names.contains(std::string{definition.name()})) {
                throw std::runtime_error{
                    "Approved batch snapshot is missing a required plugin input"
                };
            }
        }
        step.depends_on.assign(dependencies.begin(), dependencies.end());

        std::set<std::string, std::less<>> output_names;
        for (const BatchPlanOutputSnapshot& output : node.outputs) {
            const auto* definition = find_output(*resolved, output.port_name);
            if (definition == nullptr ||
                definition->file_type() != output.artifact_type ||
                !output_names.emplace(output.port_name).second) {
                throw std::runtime_error{
                    "Approved batch output snapshot no longer matches the plugin contract"
                };
            }
            ExecutionOutputBinding binding{
                .port_name = output.port_name,
                .file_type = output.artifact_type,
                .relative_project_path = output_relative_path(
                    job_id, node.node_id, output.port_name
                ),
            };
            outputs.emplace(
                std::pair<std::string, std::string>{node.node_id, output.port_name},
                binding
            );
            step.outputs.push_back(std::move(binding));
        }
        if (output_names.size() != resolved->outputs.size()) {
            throw std::runtime_error{
                "Approved batch snapshot does not contain the complete plugin output contract"
            };
        }

        steps.push_back(std::move(step));
    }

    return ExecutionPlan{
        ExecutionPlan::current_schema_version,
        std::string{job_id},
        job_revision,
        std::string{batch_pipeline_id},
        std::string{batch_pipeline_version},
        std::move(steps),
    };
}

void discard_all(
    IExecutionPlanStore& store,
    const std::vector<std::string>& paths
) noexcept {
    for (const std::string& path : paths) {
        try {
            store.discard(path);
        } catch (...) {
        }
    }
}

[[nodiscard]] bool cancellable_without_worker(
    const domain::JobStatus status
) noexcept {
    return status == domain::JobStatus::draft ||
           status == domain::JobStatus::queued ||
           status == domain::JobStatus::interrupted;
}

[[nodiscard]] bool needs_worker_cancellation(
    const domain::JobStatus status
) noexcept {
    return status == domain::JobStatus::preparing ||
           status == domain::JobStatus::running ||
           status == domain::JobStatus::paused;
}

}  // namespace

BatchExecutionService::BatchExecutionService(
    IBatchPlanStore& plans,
    IBatchExecutionStore& executions,
    const IPluginRegistry& plugins,
    IManagedFileRepository& managed_files,
    const IInputFileStorage& input_storage,
    IExecutionPlanStore& execution_plans,
    IIdGenerator& id_generator,
    IUtcClock& clock,
    JobService& jobs,
    JobScheduler& scheduler
) noexcept
    : plans_{plans},
      executions_{executions},
      plugins_{plugins},
      managed_files_{managed_files},
      input_storage_{input_storage},
      execution_plans_{execution_plans},
      id_generator_{id_generator},
      clock_{clock},
      jobs_{jobs},
      scheduler_{scheduler} {}

BatchExecutionSnapshot BatchExecutionService::submit(
    const std::string_view plan_id,
    const std::size_t maximum_concurrent_jobs,
    const domain::JobPriority priority
) {
    if (maximum_concurrent_jobs == 0U ||
        maximum_concurrent_jobs > scheduler_.maximum_concurrent_jobs()) {
        throw std::invalid_argument{
            "Batch concurrency must be between 1 and the active scheduler capacity"
        };
    }

    if (auto existing = executions_.find(plan_id); existing.has_value()) {
        if (existing->maximum_concurrent_jobs != maximum_concurrent_jobs) {
            throw std::invalid_argument{
                "Approved batch plan is already submitted with a different concurrency limit"
            };
        }
        return snapshot(*existing);
    }

    const auto plan = plans_.find(plan_id);
    if (!plan.has_value()) {
        throw std::invalid_argument{"Approved batch plan was not found"};
    }

    std::vector<const ApprovedBatchSamplePlan*> included;
    for (const ApprovedBatchSamplePlan& sample : plan->samples) {
        if (sample.disposition == BatchPlanSampleDisposition::included) {
            included.push_back(&sample);
        }
    }
    if (included.empty()) {
        throw std::logic_error{"Approved batch plan contains no included samples"};
    }

    for (int attempt = 0; attempt < maximum_identifier_attempts; ++attempt) {
        const std::string submitted_at = clock_.now_utc_iso8601();
        std::vector<BatchPreparedJob> prepared_jobs;
        std::vector<std::string> snapshot_paths;
        std::set<std::string, std::less<>> generated_ids;
        prepared_jobs.reserve(included.size());
        snapshot_paths.reserve(included.size());

        bool duplicate_generated_id = false;
        try {
            for (std::size_t ordinal = 0U; ordinal < included.size(); ++ordinal) {
                const ApprovedBatchSamplePlan& sample = *included[ordinal];
                const std::string job_id = id_generator_.generate();
                if (!generated_ids.emplace(job_id).second) {
                    duplicate_generated_id = true;
                    break;
                }

                domain::Job job{
                    job_id,
                    std::string{plan->plan_id},
                    std::string{batch_pipeline_id},
                    std::string{batch_pipeline_version},
                    domain::JobStatus::draft,
                    priority,
                    0.0,
                    std::nullopt,
                    submitted_at,
                    submitted_at,
                    std::nullopt,
                    std::nullopt,
                    0,
                };

                constexpr std::int64_t prepared_revision = 2;
                const ExecutionPlan execution_plan = materialize_execution_plan(
                    sample,
                    job_id,
                    prepared_revision,
                    plugins_,
                    managed_files_,
                    input_storage_
                );
                const std::string snapshot_path =
                    execution_plans_.store(execution_plan);
                snapshot_paths.push_back(snapshot_path);

                job.transition_to(
                    domain::JobStatus::queued,
                    0.0,
                    std::nullopt,
                    submitted_at
                );

                BatchExecutionJobLink link{
                    .sample_id = sample.sample_id,
                    .ordinal = ordinal,
                    .job_id = job_id,
                };
                prepared_jobs.push_back(BatchPreparedJob{
                    .link = link,
                    .job = std::move(job),
                    .execution = PreparedJobExecution{
                        .job_id = job_id,
                        .attempt_number = 1,
                        .launch_revision = prepared_revision,
                        .pipeline_id = std::string{batch_pipeline_id},
                        .pipeline_version = std::string{batch_pipeline_version},
                        .execution_plan_path = snapshot_path,
                        .prepared_at_utc = submitted_at,
                    },
                });
            }

            if (duplicate_generated_id) {
                discard_all(execution_plans_, snapshot_paths);
                continue;
            }

            BatchExecutionRecord record{
                .plan_id = std::string{plan_id},
                .maximum_concurrent_jobs = maximum_concurrent_jobs,
                .cancellation_requested = false,
                .submitted_at_utc = submitted_at,
                .updated_at_utc = submitted_at,
                .jobs = {},
            };
            record.jobs.reserve(prepared_jobs.size());
            for (const auto& item : prepared_jobs) {
                record.jobs.push_back(item.link);
            }

            const AddBatchExecutionResult result =
                executions_.add(record, prepared_jobs);
            if (result == AddBatchExecutionResult::created) {
                return snapshot(record);
            }

            discard_all(execution_plans_, snapshot_paths);
            if (result == AddBatchExecutionResult::plan_already_submitted) {
                const auto existing = executions_.find(plan_id);
                if (!existing.has_value()) {
                    throw std::runtime_error{
                        "Concurrent batch submission committed but cannot be reloaded"
                    };
                }
                if (existing->maximum_concurrent_jobs != maximum_concurrent_jobs) {
                    throw std::invalid_argument{
                        "Approved batch plan was concurrently submitted with a different concurrency limit"
                    };
                }
                return snapshot(*existing);
            }
        } catch (...) {
            discard_all(execution_plans_, snapshot_paths);
            throw;
        }
    }

    throw std::runtime_error{
        "Unable to allocate unique job identifiers for batch submission"
    };
}

std::optional<BatchExecutionSnapshot> BatchExecutionService::find(
    const std::string_view plan_id
) {
    const auto execution = executions_.find(plan_id);
    if (!execution.has_value()) return std::nullopt;
    return snapshot(*execution);
}

BatchExecutionSnapshot BatchExecutionService::cancel(
    const std::string_view plan_id
) {
    auto execution = executions_.find(plan_id);
    if (!execution.has_value()) {
        throw std::invalid_argument{"Batch execution was not found"};
    }

    const std::string updated_at = clock_.now_utc_iso8601();
    if (!executions_.request_cancellation(plan_id, updated_at)) {
        throw std::runtime_error{"Batch execution disappeared during cancellation"};
    }

    for (const BatchExecutionJobLink& link : execution->jobs) {
        auto current = jobs_.find_by_id(link.job_id);
        if (!current.has_value()) {
            throw std::runtime_error{"Batch execution references a missing job"};
        }

        const auto status = current->status();
        if (domain::is_terminal(status) || status == domain::JobStatus::cancelling) {
            continue;
        }

        const domain::JobStatus target = cancellable_without_worker(status)
            ? domain::JobStatus::cancelled
            : (needs_worker_cancellation(status)
                ? domain::JobStatus::cancelling
                : status);
        if (target == status) continue;

        try {
            static_cast<void>(jobs_.transition(
                link.job_id,
                target,
                current->progress(),
                std::nullopt
            ));
        } catch (const JobServiceError& error) {
            if (error.code() != JobServiceErrorCode::concurrent_update) throw;
            const auto raced = jobs_.find_by_id(link.job_id);
            if (!raced.has_value() ||
                (!domain::is_terminal(raced->status()) &&
                 raced->status() != domain::JobStatus::cancelling)) {
                throw;
            }
        }
    }

    execution = executions_.find(plan_id);
    if (!execution.has_value()) {
        throw std::runtime_error{"Batch execution disappeared after cancellation"};
    }
    return snapshot(*execution);
}

BatchExecutionSnapshot BatchExecutionService::snapshot(
    const BatchExecutionRecord& execution
) {
    BatchExecutionSnapshot result{
        .plan_id = execution.plan_id,
        .maximum_concurrent_jobs = execution.maximum_concurrent_jobs,
        .cancellation_requested = execution.cancellation_requested,
        .state = BatchExecutionState::active,
        .completed_count = 0U,
        .failed_count = 0U,
        .cancelled_count = 0U,
        .interrupted_count = 0U,
        .active_count = 0U,
        .samples = {},
    };
    result.samples.reserve(execution.jobs.size());

    for (const BatchExecutionJobLink& link : execution.jobs) {
        const auto job = jobs_.find_by_id(link.job_id);
        if (!job.has_value()) {
            throw std::runtime_error{"Batch execution references a missing job"};
        }

        switch (job->status()) {
            case domain::JobStatus::completed:
                ++result.completed_count;
                break;
            case domain::JobStatus::failed:
                ++result.failed_count;
                break;
            case domain::JobStatus::cancelled:
                ++result.cancelled_count;
                break;
            case domain::JobStatus::interrupted:
                ++result.interrupted_count;
                break;
            default:
                ++result.active_count;
                break;
        }

        result.samples.push_back(BatchExecutionSampleView{
            .sample_id = link.sample_id,
            .job_id = link.job_id,
            .status = job->status(),
            .progress = job->progress(),
            .attempt_number = job->attempt_number(),
        });
    }

    if (execution.cancellation_requested) {
        result.state =
            result.active_count != 0U || result.interrupted_count != 0U
            ? BatchExecutionState::cancelling
            : BatchExecutionState::cancelled;
    } else if (result.active_count != 0U) {
        result.state = BatchExecutionState::active;
    } else if (result.interrupted_count != 0U) {
        result.state = BatchExecutionState::attention;
    } else if (result.completed_count == result.samples.size()) {
        result.state = BatchExecutionState::completed;
    } else if (result.failed_count == result.samples.size()) {
        result.state = BatchExecutionState::failed;
    } else {
        result.state = BatchExecutionState::partial_failure;
    }

    return result;
}

}  // namespace biocore::application
