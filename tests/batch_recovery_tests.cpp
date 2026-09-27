#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/batch_execution_service.hpp"
#include "biocore/application/batch_recovery_service.hpp"
#include "biocore/application/i_execution_plan_store.hpp"
#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_input_file_storage.hpp"
#include "biocore/application/i_plugin_registry.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/i_workflow_checkpoint_artifact_verifier.hpp"
#include "biocore/application/i_worker_supervisor.hpp"
#include "biocore/application/job_scheduler.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/domain/job_failure.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/project.hpp"
#include "biocore/domain/storage_mode.hpp"
#include "biocore/infrastructure/sqlite/project_database_initializer.hpp"
#include "biocore/infrastructure/sqlite/sqlite_batch_execution_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_batch_plan_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_job_repository.hpp"
#include "biocore/infrastructure/sqlite/sqlite_managed_file_repository.hpp"
#include "biocore/infrastructure/sqlite/sqlite_prepared_job_store.hpp"

namespace {

using namespace biocore;
using namespace biocore::infrastructure::sqlite;

constexpr const char* stamp = "2026-09-28T00:30:00Z";
constexpr const char* project_id = "p-084";
constexpr const char* plan_id = "batch.plan.084";

void check(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

template<class Exception, class Function>
void rejects(Function function) {
    try {
        function();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error{"Expected rejection"};
}

class SequenceIds final : public application::IIdGenerator {
public:
    explicit SequenceIds(std::vector<std::string> values)
        : values_{std::move(values)} {}

    std::string generate() override {
        if (index_ >= values_.size()) throw std::runtime_error{"ID sequence exhausted"};
        return values_[index_++];
    }

private:
    std::vector<std::string> values_;
    std::size_t index_{0U};
};

class Clock final : public application::IUtcClock {
public:
    std::string now_utc_iso8601() override {
        return "2026-09-28T00:30:" + two_digit(counter_++) + "Z";
    }
private:
    static std::string two_digit(const int value) {
        const int bounded = value % 60;
        return bounded < 10 ? "0" + std::to_string(bounded) : std::to_string(bounded);
    }
    int counter_{0};
};

class InputStorage final : public application::IInputFileStorage {
public:
    std::unique_ptr<application::IInputFileImportTransaction> prepare_managed_copy(
        std::string_view, std::string_view
    ) override { throw std::logic_error{"unused"}; }
    bool begin_browser_upload(std::string_view, std::string_view) override {
        throw std::logic_error{"unused"};
    }
    std::uint64_t append_browser_upload(
        std::string_view, std::uint64_t, std::string_view
    ) override { throw std::logic_error{"unused"}; }
    std::unique_ptr<application::IInputFileImportTransaction> prepare_browser_upload_commit(
        std::string_view, std::string_view
    ) override { throw std::logic_error{"unused"}; }
    void discard_browser_upload(std::string_view) noexcept override {}

    application::ManagedFileIntegrityResult verify_managed_file(
        const domain::ManagedFile& file
    ) const override {
        return {
            .status = valid
                ? application::ManagedFileIntegrityStatus::verified
                : application::ManagedFileIntegrityStatus::checksum_mismatch,
            .expected_size_bytes = file.size_bytes(),
            .observed_size_bytes = file.size_bytes(),
            .expected_sha256 = file.checksum_value(),
            .observed_sha256 = valid ? file.checksum_value()
                                     : std::optional<std::string>{std::string(64U, 'f')},
        };
    }

    bool valid{true};
};

class PluginRegistry final : public application::IPluginRegistry {
public:
    std::optional<application::ResolvedPluginModule> find_module(
        const std::string_view module_id
    ) const override {
        if (module_id == "org.test.step1") {
            return application::ResolvedPluginModule{
                .plugin_id = "org.test",
                .plugin_version = "1.0.0",
                .plugin_manifest_version = 2U,
                .plugin_api_version = "1.0",
                .module_id = "org.test.step1",
                .module_type = domain::PluginModuleType::process,
                .plugin_root_path = "/plugins/org.test",
                .executable_path = "/plugins/org.test/bin/step1",
                .parameters = {},
                .inputs = {domain::PluginInputPortDefinition{"reads", true, {"fastq"}}},
                .outputs = {domain::PluginOutputPortDefinition{"mid", "txt"}},
            };
        }
        if (module_id == "org.test.step2") {
            return application::ResolvedPluginModule{
                .plugin_id = "org.test",
                .plugin_version = "1.0.0",
                .plugin_manifest_version = 2U,
                .plugin_api_version = "1.0",
                .module_id = "org.test.step2",
                .module_type = domain::PluginModuleType::process,
                .plugin_root_path = "/plugins/org.test",
                .executable_path = "/plugins/org.test/bin/step2",
                .parameters = {},
                .inputs = {domain::PluginInputPortDefinition{"mid", true, {"txt"}}},
                .outputs = {domain::PluginOutputPortDefinition{"report", "json"}},
            };
        }
        return std::nullopt;
    }
    std::vector<application::RegisteredPlugin> list_plugins() const override { return {}; }
};

class ExecutionPlanStore final : public application::IExecutionPlanStore {
public:
    std::string store(const application::ExecutionPlan& plan) override {
        plans.push_back(plan);
        return "/tmp/" + std::string{plan.job_id()} + ".json";
    }
    void discard(const std::string_view path) override { discarded.emplace_back(path); }
    std::vector<application::ExecutionPlan> plans;
    std::vector<std::string> discarded;
};

class Verifier final : public application::IWorkflowCheckpointArtifactVerifier {
public:
    bool verify(const domain::WorkflowCheckpointArtifact& artifact) const override {
        return valid && !artifact.relative_project_path.empty() && artifact.size_bytes > 0 &&
               artifact.sha256.size() == 64U;
    }
    bool valid{true};
};

class Supervisor final : public application::IWorkerSupervisor {
public:
    void launch(const application::WorkerLaunchRequest& request) override {
        launches.push_back(request);
    }
    std::vector<application::WorkerLaunchRequest> launches;
};

struct Harness final {
    SqliteConnection connection{":memory:"};
    SqliteBatchPlanStore plans{connection};
    SqliteBatchExecutionStore executions{connection};
    SqliteManagedFileRepository files{connection};
    SqliteJobRepository job_repository{connection};
    SqlitePreparedJobStore prepared_jobs{connection};
    SequenceIds ids;
    Clock clock;
    application::JobService jobs{job_repository, ids, clock};
    InputStorage input_storage;
    PluginRegistry plugins;
    ExecutionPlanStore execution_plans;
    Verifier verifier;
    Supervisor supervisor;
    application::JobScheduler scheduler{jobs, prepared_jobs, supervisor, 8U, &executions};
    application::BatchExecutionService execution_service{
        plans, executions, plugins, files, input_storage, execution_plans,
        ids, clock, jobs, scheduler
    };
    application::BatchRecoveryService recovery_service{
        plans, executions, plugins, files, input_storage, verifier,
        execution_plans, ids, clock, jobs
    };

    explicit Harness(std::vector<std::string> ids_values)
        : ids{std::move(ids_values)} {
        ProjectDatabaseInitializer{connection}.initialize(
            domain::Project{project_id, "Recovery", "/local/recovery", stamp, stamp}
        );
        seed();
    }

    void seed() {
        const domain::ManagedFile input{
            "input-1", "input.fastq", domain::StorageMode::managed_copy,
            "/original/input.fastq", "/project/inputs/input.fastq", "inputs/input.fastq",
            "fastq", 4, std::nullopt, std::string{"sha256"}, std::string(64U, 'a'),
            stamp, stamp
        };
        check(files.add(input), "input seed failed");

        application::ApprovedBatchPlan plan{
            .plan_id = plan_id,
            .project_id = project_id,
            .template_id = "org.test.template",
            .template_version = "1.0.0",
            .approved_at_utc = stamp,
            .samples = {
                application::ApprovedBatchSamplePlan{
                    .sample_id = "S1",
                    .disposition = application::BatchPlanSampleDisposition::included,
                    .workflow_id = "batch.plan.084.sample.s1",
                    .nodes = {
                        application::BatchPlanNodeSnapshot{
                            .node_id = "step1",
                            .module_id = "org.test.step1",
                            .plugin_version = "1.0.0",
                            .parameters = {},
                            .inputs = {
                                application::BatchPlanInputSnapshot{
                                    .port_name = "reads",
                                    .artifact_type = "fastq",
                                    .source_kind = application::BatchPlanInputSourceKind::managed_file,
                                    .source_id = "input-1",
                                    .source_port = "",
                                    .managed_file = application::BatchPlanManagedFileSnapshot{
                                        .role = application::BatchInputFileRole::primary,
                                        .file_id = "input-1",
                                        .file_type = "fastq",
                                        .size_bytes = 4,
                                        .sha256 = std::string(64U, 'a'),
                                    },
                                },
                            },
                            .outputs = {
                                application::BatchPlanOutputSnapshot{
                                    .port_name = "mid", .artifact_type = "txt"
                                },
                            },
                        },
                        application::BatchPlanNodeSnapshot{
                            .node_id = "step2",
                            .module_id = "org.test.step2",
                            .plugin_version = "1.0.0",
                            .parameters = {},
                            .inputs = {
                                application::BatchPlanInputSnapshot{
                                    .port_name = "mid",
                                    .artifact_type = "txt",
                                    .source_kind = application::BatchPlanInputSourceKind::node_output,
                                    .source_id = "step1",
                                    .source_port = "mid",
                                    .managed_file = std::nullopt,
                                },
                            },
                            .outputs = {
                                application::BatchPlanOutputSnapshot{
                                    .port_name = "report", .artifact_type = "json"
                                },
                            },
                        },
                    },
                },
            },
        };
        check(plans.add(plan), "plan seed failed");
    }
};

void register_output(
    Harness& harness,
    const std::string_view job_id,
    const application::ExecutionPlanStep& step,
    const double progress
) {
    check(step.outputs.size() == 1U, "test step output shape changed");
    const auto& output = step.outputs.front();
    const std::string file_id = std::string{job_id} + "-" + step.id + "-" + output.port_name;
    domain::ManagedFile file{
        file_id,
        file_id + ".out",
        domain::StorageMode::generated_output,
        std::nullopt,
        "/project/" + output.relative_project_path,
        output.relative_project_path,
        output.file_type,
        4,
        std::nullopt,
        std::string{"sha256"},
        std::string(64U, 'b'),
        stamp,
        stamp,
    };
    check(harness.files.add_generated_output(
        file,
        application::GeneratedOutputProvenance{
            .job_id = std::string{job_id},
            .step_id = step.id,
            .output_port = output.port_name,
            .plugin_id = step.plugin_id,
            .plugin_version = step.plugin_version,
            .module_id = step.module_id,
            .file_type = output.file_type,
            .relative_project_path = output.relative_project_path,
            .step_progress = progress,
            .registered_at_utc = stamp,
        }
    ), "generated output registration failed");
}

void start_job(Harness& harness, const std::string_view job_id) {
    static_cast<void>(harness.jobs.transition(
        job_id, domain::JobStatus::preparing, 0.0, std::nullopt
    ));
    static_cast<void>(harness.jobs.transition(
        job_id, domain::JobStatus::running, 0.0, std::string{"step1"}
    ));
}

void interrupt_after_first(Harness& harness) {
    static_cast<void>(harness.execution_service.submit(plan_id, 1U));
    check(harness.execution_plans.plans.size() == 1U, "initial plan missing");
    const auto& plan = harness.execution_plans.plans.front();
    start_job(harness, "job-a");
    register_output(harness, "job-a", plan.steps()[0], 0.5);
    static_cast<void>(harness.jobs.update_progress("job-a", 0.5, std::string{"step1"}));
    static_cast<void>(harness.jobs.transition(
        "job-a", domain::JobStatus::interrupted, 0.5, std::nullopt,
        application::JobFailureContext{
            .kind = domain::JobFailureKind::startup_recovery,
            .message = "synthetic restart interruption",
            .exit_code = std::nullopt,
            .worker_timestamp_utc = std::nullopt,
        }
    ));
}

void restart_reconciliation_contract() {
    Harness harness{{"job-a", "job-b"}};
    interrupt_after_first(harness);

    const auto before_jobs = harness.jobs.list().size();
    application::BatchRecoveryService restarted{
        harness.plans, harness.executions, harness.plugins, harness.files,
        harness.input_storage, harness.verifier, harness.execution_plans,
        harness.ids, harness.clock, harness.jobs
    };
    const auto inspections = restarted.inspect_all();
    check(inspections.size() == 1U && inspections[0].samples.size() == 1U,
          "restart inspection did not restore batch state");
    check(inspections[0].samples[0].action == application::BatchRecoveryAction::resume,
          "restart inspection did not distinguish resumable checkpoint");
    check(harness.jobs.list().size() == before_jobs,
          "restart inspection auto-reran interrupted work");

    const auto snapshot = harness.execution_service.find(plan_id);
    check(snapshot.has_value() && snapshot->state == application::BatchExecutionState::attention &&
          snapshot->interrupted_count == 1U,
          "interrupted batch state was not restored as attention");
}

void resume_contract() {
    Harness harness{{"job-a", "job-b"}};
    interrupt_after_first(harness);
    const auto attempt = harness.recovery_service.resume(plan_id, "S1");

    check(attempt.attempt_number == 2 &&
          attempt.mode == application::BatchAttemptMode::resume &&
          attempt.parent_job_id == std::optional<std::string>{"job-a"} &&
          attempt.job_id == "job-b" &&
          attempt.execution_node_ids == std::vector<std::string>{"step2"},
          "resume lineage is incorrect");
    check(harness.execution_plans.plans.size() == 2U,
          "resume did not create one child execution plan");
    const auto& resumed = harness.execution_plans.plans.back();
    check(resumed.steps().size() == 1U && resumed.steps()[0].id == "step2",
          "resume reran an already completed node");
    check(resumed.steps()[0].depends_on.empty(),
          "resume retained a dependency on a reused node");
    check(
        resumed.steps()[0].inputs[0].relative_project_path.find("job-a--step1--mid.out")
            != std::string::npos,
        "resume did not bind verified checkpoint output"
    );
    const auto quota = harness.executions.quota_for_job("job-b");
    check(quota.has_value() && quota->group_id == plan_id &&
          quota->maximum_concurrent_jobs == 1U,
          "resume child escaped batch scheduler quota");
    const auto snapshot = harness.execution_service.find(plan_id);
    check(snapshot.has_value() && snapshot->samples[0].job_id == "job-b" &&
          snapshot->samples[0].attempt_number == 2 &&
          snapshot->samples[0].attempt_mode == application::BatchAttemptMode::resume,
          "batch snapshot did not advance to resume attempt");
}

void completed_checkpoint_contract() {
    Harness harness{{"job-a", "job-b"}};
    static_cast<void>(harness.execution_service.submit(plan_id, 1U));
    const auto& plan = harness.execution_plans.plans.front();
    start_job(harness, "job-a");
    register_output(harness, "job-a", plan.steps()[0], 0.5);
    register_output(harness, "job-a", plan.steps()[1], 1.0);
    static_cast<void>(harness.jobs.update_progress("job-a", 0.5, std::string{"step1"}));
    static_cast<void>(harness.jobs.update_progress("job-a", 1.0, std::string{"step2"}));
    static_cast<void>(harness.jobs.transition(
        "job-a", domain::JobStatus::interrupted, 1.0, std::nullopt,
        application::JobFailureContext{
            .kind = domain::JobFailureKind::startup_recovery,
            .message = "crash after durable outputs",
            .exit_code = std::nullopt,
            .worker_timestamp_utc = std::nullopt,
        }
    ));

    const auto attempt = harness.recovery_service.resume(plan_id, "S1");
    check(attempt.execution_node_ids.empty(),
          "checkpoint-complete resume scheduled unnecessary work");
    check(harness.execution_plans.plans.size() == 1U,
          "checkpoint-complete resume wrote a worker plan");
    const auto child = harness.jobs.find_by_id("job-b");
    check(child.has_value() && child->status() == domain::JobStatus::completed,
          "checkpoint-complete resume did not seal a completed child attempt");
    const auto snapshot = harness.execution_service.find(plan_id);
    check(snapshot.has_value() && snapshot->state == application::BatchExecutionState::completed,
          "checkpoint-complete recovery did not restore completed batch state");
}

void retry_contract() {
    Harness harness{{"job-a", "job-b"}};
    static_cast<void>(harness.execution_service.submit(plan_id, 1U));
    start_job(harness, "job-a");
    static_cast<void>(harness.jobs.transition(
        "job-a", domain::JobStatus::failed, 0.0, std::nullopt,
        application::JobFailureContext{
            .kind = domain::JobFailureKind::worker_reported_failure,
            .message = "synthetic failure",
            .exit_code = 2,
            .worker_timestamp_utc = std::string{stamp},
        }
    ));
    const auto inspection = harness.recovery_service.inspect(plan_id);
    check(inspection.samples[0].action == application::BatchRecoveryAction::retry,
          "failed sample was not classified as retry");

    const auto attempt = harness.recovery_service.retry(plan_id, "S1");
    check(attempt.attempt_number == 2 &&
          attempt.mode == application::BatchAttemptMode::retry &&
          attempt.parent_job_id == std::optional<std::string>{"job-a"} &&
          attempt.job_id == "job-b" &&
          attempt.execution_node_ids == std::vector<std::string>({"step1", "step2"}),
          "retry did not create a linked full child attempt");
    check(harness.execution_plans.plans.back().steps().size() == 2U,
          "retry did not rebuild the full frozen execution plan");
    check(harness.jobs.find_by_id("job-a")->status() == domain::JobStatus::failed,
          "retry mutated the historical failed job");
}

void changed_input_contract() {
    Harness harness{{"job-a", "job-b"}};
    interrupt_after_first(harness);
    harness.input_storage.valid = false;
    const auto inspection = harness.recovery_service.inspect(plan_id);
    check(inspection.samples[0].action == application::BatchRecoveryAction::blocked,
          "changed input did not block checkpoint reuse");
    rejects<std::runtime_error>([&] {
        static_cast<void>(harness.recovery_service.resume(plan_id, "S1"));
    });
    check(harness.executions.list_attempts(plan_id).size() == 1U,
          "blocked resume created a new attempt");
    check(harness.jobs.find_by_id("job-b") == std::nullopt,
          "blocked resume created a child job");
}

void checkpoint_integrity_contract() {
    Harness harness{{"job-a", "job-b"}};
    interrupt_after_first(harness);
    harness.verifier.valid = false;
    const auto inspection = harness.recovery_service.inspect(plan_id);
    check(inspection.samples[0].action == application::BatchRecoveryAction::retry,
          "invalid checkpoint was silently classified as resumable");
    rejects<std::logic_error>([&] {
        static_cast<void>(harness.recovery_service.resume(plan_id, "S1"));
    });
    check(harness.executions.list_attempts(plan_id).size() == 1U,
          "checkpoint-integrity failure created a resume attempt");
}

void cancel_latest_attempt_contract() {
    Harness harness{{"job-a", "job-b"}};
    interrupt_after_first(harness);
    static_cast<void>(harness.recovery_service.resume(plan_id, "S1"));

    const auto before = harness.jobs.find_by_id("job-b");
    check(before.has_value() && before->status() == domain::JobStatus::queued,
          "resume child was not queued before cancellation");

    const auto cancelled = harness.execution_service.cancel(plan_id);
    check(cancelled.cancellation_requested,
          "batch cancellation request was not persisted");
    const auto child = harness.jobs.find_by_id("job-b");
    check(child.has_value() && child->status() == domain::JobStatus::cancelled,
          "batch cancellation did not target the latest child attempt");
    const auto root = harness.jobs.find_by_id("job-a");
    check(root.has_value() && root->status() == domain::JobStatus::interrupted,
          "batch cancellation mutated historical attempt state");
    check(cancelled.samples[0].job_id == "job-b" &&
          cancelled.samples[0].attempt_number == 2 &&
          cancelled.samples[0].status == domain::JobStatus::cancelled,
          "cancelled snapshot did not resolve to latest attempt");
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        if (argc != 2) return EXIT_FAILURE;
        const std::string_view mode{argv[1]};
        if (mode == "restart") restart_reconciliation_contract();
        else if (mode == "resume") resume_contract();
        else if (mode == "checkpoint-complete") completed_checkpoint_contract();
        else if (mode == "retry") retry_contract();
        else if (mode == "changed-input") changed_input_contract();
        else if (mode == "checkpoint-integrity") checkpoint_integrity_contract();
        else if (mode == "cancel-latest") cancel_latest_attempt_contract();
        else return EXIT_FAILURE;
        std::cout << mode << " PASS\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
