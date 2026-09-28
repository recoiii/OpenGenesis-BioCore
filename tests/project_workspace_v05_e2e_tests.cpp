#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/batch_execution_service.hpp"
#include "biocore/application/batch_planning_service.hpp"
#include "biocore/application/batch_recovery_service.hpp"
#include "biocore/application/batch_result_package_service.hpp"
#include "biocore/application/batch_results_service.hpp"
#include "biocore/application/i_artifact_content_access.hpp"
#include "biocore/application/i_execution_plan_store.hpp"
#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_input_file_storage.hpp"
#include "biocore/application/i_plugin_registry.hpp"
#include "biocore/application/i_reference_compatibility_inspector.hpp"
#include "biocore/application/i_result_artifact_reader.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/i_worker_supervisor.hpp"
#include "biocore/application/i_workflow_checkpoint_artifact_verifier.hpp"
#include "biocore/application/i_workflow_template_catalog.hpp"
#include "biocore/application/job_scheduler.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/project_workspace_integration_service.hpp"
#include "biocore/application/sample_binding_service.hpp"
#include "biocore/application/sample_registry_import.hpp"
#include "biocore/domain/job_failure.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/project.hpp"
#include "biocore/domain/storage_mode.hpp"
#include "biocore/domain/workflow.hpp"
#include "biocore/domain/workflow_template.hpp"
#include "biocore/infrastructure/sqlite/project_database_initializer.hpp"
#include "biocore/infrastructure/sqlite/sqlite_batch_execution_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_batch_plan_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_connection.hpp"
#include "biocore/infrastructure/sqlite/sqlite_current_project_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_job_repository.hpp"
#include "biocore/infrastructure/sqlite/sqlite_managed_file_repository.hpp"
#include "biocore/infrastructure/sqlite/sqlite_prepared_job_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_project_research_metadata_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_project_sample_binding_store.hpp"
#include "biocore/infrastructure/sqlite/sqlite_project_sample_store.hpp"
#include "biocore/presentation/batch_result_package_report.hpp"

namespace {
using namespace biocore;
using namespace biocore::infrastructure::sqlite;

constexpr const char* project_id = "project-087";
constexpr const char* plan_id = "batch.plan.087";
constexpr const char* stamp = "2026-09-28T08:30:00Z";

void check(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

class SequenceIds final : public application::IIdGenerator {
public:
    explicit SequenceIds(std::vector<std::string> values) : values_{std::move(values)} {}
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
        const int seconds = counter_++ % 60;
        return std::string{"2026-09-28T08:30:"} +
               (seconds < 10 ? "0" : "") + std::to_string(seconds) + "Z";
    }
private:
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
            .status = application::ManagedFileIntegrityStatus::verified,
            .expected_size_bytes = file.size_bytes(),
            .observed_size_bytes = file.size_bytes(),
            .expected_sha256 = file.checksum_value(),
            .observed_sha256 = file.checksum_value(),
        };
    }
};

class ReferenceInspector final : public application::IReferenceCompatibilityInspector {
public:
    application::ReferenceCompatibilityResult inspect(
        const domain::ManagedFile&, const domain::ManagedFile&
    ) const override {
        return {application::ReferenceCompatibilityStatus::verified, "verified"};
    }
};

class TemplateCatalog final : public application::IWorkflowTemplateCatalog {
public:
    std::optional<domain::WorkflowTemplate> find(
        const std::string_view id, const std::string_view version
    ) const override {
        if (id != "org.biocore.template.workspace-e2e" || version != "1.0.0") {
            return std::nullopt;
        }
        return domain::WorkflowTemplate{
            1U,
            "org.biocore.template.workspace-e2e",
            "1.0.0",
            "Workspace E2E",
            "Iteration 087 end-to-end fixture",
            domain::Workflow{
                1U,
                domain::WorkflowId{"org.biocore.template.workspace-e2e"},
                "Workspace E2E",
                "",
                {
                    domain::WorkflowNode{
                        domain::WorkflowNodeId{"analyze"},
                        "Analyze",
                        "org.biocore.workspace-e2e.analyze",
                        "1.0.0",
                        {domain::WorkflowInputDeclaration{"reads", "fastq", true}},
                        {domain::WorkflowOutputDeclaration{"result", "json"}},
                        {}
                    },
                },
                {}
            }
        };
    }

    std::vector<application::RegisteredWorkflowTemplate> list() const override {
        return {{
            "org.biocore.template.workspace-e2e", "1.0.0", "Workspace E2E",
            "Iteration 087 end-to-end fixture"
        }};
    }
};

class PluginRegistry final : public application::IPluginRegistry {
public:
    std::optional<application::ResolvedPluginModule> find_module(
        const std::string_view module_id
    ) const override {
        if (module_id != "org.biocore.workspace-e2e.analyze") return std::nullopt;
        return application::ResolvedPluginModule{
            .plugin_id = "org.biocore.workspace-e2e",
            .plugin_version = "1.0.0",
            .plugin_manifest_version = 2U,
            .plugin_api_version = "1.0",
            .module_id = "org.biocore.workspace-e2e.analyze",
            .module_type = domain::PluginModuleType::process,
            .plugin_root_path = "/plugins/org.biocore.workspace-e2e",
            .executable_path = "/plugins/org.biocore.workspace-e2e/bin/analyze",
            .parameters = {},
            .inputs = {domain::PluginInputPortDefinition{"reads", true, {"fastq"}}},
            .outputs = {domain::PluginOutputPortDefinition{"result", "json"}},
        };
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

class CheckpointVerifier final : public application::IWorkflowCheckpointArtifactVerifier {
public:
    bool verify(const domain::WorkflowCheckpointArtifact&) const override { return false; }
};

class Supervisor final : public application::IWorkerSupervisor {
public:
    void launch(const application::WorkerLaunchRequest& request) override {
        launches.push_back(request);
    }
    std::vector<application::WorkerLaunchRequest> launches;
};

class ResultReader final : public application::IResultArtifactReader {
public:
    application::ResultArtifactText read_verified_text(
        const application::GeneratedOutputArtifact&, std::size_t
    ) override {
        return {.status = application::ResultArtifactReadStatus::missing, .text = std::nullopt, .verified_sha256 = std::nullopt};
    }
};

class ContentAccess final : public application::IArtifactContentAccess {
public:
    application::ArtifactContentVerification verify_for_download(
        const application::GeneratedOutputArtifact&
    ) override {
        return {.status = application::ArtifactContentStatus::missing, .content_path = std::nullopt, .computed_sha256 = std::nullopt, .actual_size_bytes = 0};
    }
};

struct Harness final {
    SqliteConnection connection{":memory:"};
    SqliteCurrentProjectStore current_project{connection};
    SqliteProjectResearchMetadataStore research{connection};
    SqliteProjectSampleStore samples{connection};
    SqliteProjectSampleBindingStore bindings{connection};
    SqliteManagedFileRepository files{connection};
    SqliteBatchPlanStore plans{connection};
    SqliteBatchExecutionStore executions{connection};
    SqliteJobRepository job_repository{connection};
    SqlitePreparedJobStore prepared_jobs{connection};
    SequenceIds ids{{"job-initial", "job-retry"}};
    Clock clock;
    application::JobService jobs{job_repository, ids, clock};
    InputStorage input_storage;
    ReferenceInspector reference_inspector;
    TemplateCatalog templates;
    PluginRegistry plugins;
    ExecutionPlanStore execution_plans;
    CheckpointVerifier checkpoint_verifier;
    Supervisor supervisor;
    ResultReader result_reader;
    ContentAccess content_access;
    application::SampleRegistryImportService sample_import{samples};
    application::SampleBindingService sample_binding{
        samples, bindings, files, input_storage, reference_inspector
    };
    application::BatchPlanningService planning{
        samples, bindings, files, sample_binding, templates, plugins, plans
    };
    application::JobScheduler scheduler{jobs, prepared_jobs, supervisor, 4U, &executions};
    application::BatchExecutionService execution{
        plans, executions, plugins, files, input_storage, execution_plans,
        ids, clock, jobs, scheduler
    };
    application::BatchRecoveryService recovery{
        plans, executions, plugins, files, input_storage, checkpoint_verifier,
        execution_plans, ids, clock, jobs
    };
    application::BatchResultsService results{
        plans, executions, job_repository, files, result_reader
    };
    application::ProjectWorkspaceIntegrationService workspace{
        current_project, research, samples, bindings, files, sample_import,
        sample_binding, planning, plans, execution, recovery, results, clock
    };
    application::BatchResultPackageService packages{
        results, files, content_access, clock
    };

    Harness() {
        ProjectDatabaseInitializer{connection}.initialize(domain::Project{
            project_id, "Workspace 087", "/local/workspace-087", stamp, stamp
        });
        const domain::ManagedFile input{
            "reads-1", "S1.fastq", domain::StorageMode::managed_copy,
            "/source/S1.fastq", "/local/workspace-087/inputs/S1.fastq",
            "inputs/S1.fastq", "fastq", 8, std::nullopt,
            std::string{"sha256"}, std::string(64U, 'a'), stamp, stamp
        };
        check(files.add(input), "managed input seed failed");
    }
};

void complete_job(application::JobService& jobs, const std::string_view id) {
    auto current = jobs.find_by_id(id);
    check(current.has_value(), "job missing before completion");
    static_cast<void>(jobs.transition(id, domain::JobStatus::preparing, 0.0, std::nullopt));
    static_cast<void>(jobs.transition(id, domain::JobStatus::running, 0.0, std::string{"analyze"}));
    static_cast<void>(jobs.update_progress(id, 1.0, std::string{"analyze"}));
    static_cast<void>(jobs.transition(id, domain::JobStatus::completed, 1.0, std::nullopt));
}

void fail_job(application::JobService& jobs, const std::string_view id) {
    static_cast<void>(jobs.transition(id, domain::JobStatus::preparing, 0.0, std::nullopt));
    static_cast<void>(jobs.transition(id, domain::JobStatus::running, 0.0, std::string{"analyze"}));
    static_cast<void>(jobs.transition(
        id, domain::JobStatus::failed, 0.0, std::nullopt,
        application::JobFailureContext{
            .kind = domain::JobFailureKind::worker_reported_failure,
            .message = "synthetic workspace failure",
            .exit_code = 2,
            .worker_timestamp_utc = std::string{stamp},
        }
    ));
}

void e2e_contract() {
    Harness harness;

    auto snapshot = harness.workspace.snapshot();
    check(snapshot.project.id() == project_id, "current project not resolved");
    check(snapshot.samples.empty(), "new workspace should have no samples");
    check(snapshot.files.size() == 1U && snapshot.files[0].orphaned,
          "unbound managed input must be visible as orphaned");

    const std::string table =
        "sample_id,display_name,group\n"
        "S1,Sample 1,case\n";
    const auto import_preview = harness.workspace.preview_sample_import(
        table, application::SampleTableFormat::csv
    );
    check(import_preview.valid() && import_preview.samples().size() == 1U,
          "sample import preview invalid");
    const auto committed = harness.workspace.commit_sample_import(
        table, application::SampleTableFormat::csv
    );
    check(committed.valid(), "sample import commit invalid");

    snapshot = harness.workspace.snapshot();
    check(snapshot.samples.size() == 1U && !snapshot.samples[0].binding_complete,
          "imported sample must expose missing binding");
    check(!snapshot.samples[0].issues.empty() &&
          snapshot.samples[0].issues[0].code == "binding_missing",
          "missing binding must be explained");

    const application::ProjectWorkspaceBindingRequest binding_request{
        .sample_id = "S1",
        .layout = domain::SampleInputLayout::single_fastq,
        .primary_file_id = "reads-1",
        .secondary_file_id = std::nullopt,
        .reference_file_id = std::nullopt,
    };
    check(harness.workspace.preview_binding(binding_request).valid(),
          "binding preview invalid");
    check(harness.workspace.commit_binding(binding_request).valid(),
          "binding commit invalid");

    snapshot = harness.workspace.snapshot();
    check(snapshot.samples[0].binding_complete, "binding did not become complete");
    check(snapshot.files.size() == 1U && !snapshot.files[0].orphaned,
          "bound input remained orphaned");

    const auto preview = harness.workspace.preview_batch({
        .plan_id = plan_id,
        .template_id = "org.biocore.template.workspace-e2e",
        .template_version = "1.0.0",
        .sample_ids = {"S1"},
        .parameter_overrides = {},
        .input_assignments = {
            application::BatchWorkflowInputAssignment{
                .node_id = domain::WorkflowNodeId{"analyze"},
                .input_port = "reads",
                .role = application::BatchInputFileRole::primary,
            },
        },
    });
    check(preview.globally_valid() && preview.samples.size() == 1U && preview.samples[0].valid(),
          "batch preview invalid");

    const auto approved = harness.workspace.approve_batch(plan_id, {});
    check(approved.plan_id == plan_id && approved.samples.size() == 1U,
          "batch approval failed");
    const auto submitted = harness.workspace.submit_batch(
        plan_id, {.maximum_concurrent_jobs = 1U, .priority = domain::JobPriority::normal}
    );
    check(submitted.samples.size() == 1U && submitted.samples[0].job_id == "job-initial",
          "batch submit did not create expected job");

    fail_job(harness.jobs, "job-initial");
    const auto recovery = harness.workspace.inspect_recovery(plan_id);
    check(recovery.samples.size() == 1U &&
          recovery.samples[0].action == application::BatchRecoveryAction::retry,
          "failed sample was not exposed as explicit retry");

    const auto retry = harness.workspace.retry_sample(plan_id, "S1");
    check(retry.attempt_number == 2 && retry.job_id == "job-retry" &&
          retry.parent_job_id == std::optional<std::string>{"job-initial"} &&
          retry.mode == application::BatchAttemptMode::retry,
          "workspace retry did not create immutable child attempt");
    check(harness.jobs.find_by_id("job-initial")->status() == domain::JobStatus::failed,
          "retry mutated historical failed job");

    complete_job(harness.jobs, "job-retry");
    const auto overview = harness.workspace.results(plan_id);
    check(overview.samples.size() == 1U &&
          overview.samples[0].latest_job_id == std::optional<std::string>{"job-retry"} &&
          overview.samples[0].latest_attempt_number == 2 &&
          overview.samples[0].latest_job_status == std::optional<domain::JobStatus>{domain::JobStatus::completed},
          "results did not resolve latest retry attempt");

    const auto package = harness.packages.build(plan_id);
    check(package.stable_snapshot && package.complete_results &&
          package.overview.plan_id == plan_id,
          "report package did not consume completed workspace results");
    const auto json = presentation::render_batch_result_package_manifest_json(package);
    const auto html = presentation::render_batch_result_package_html(package);
    check(json.find(plan_id) != std::string::npos && json.find("job-retry") != std::string::npos,
          "JSON report lost plan/attempt provenance");
    check(html.find(plan_id) != std::string::npos && html.find("job-retry") != std::string::npos,
          "HTML report lost plan/attempt provenance");
    check(json.find("/local/workspace-087") == std::string::npos &&
          html.find("/local/workspace-087") == std::string::npos,
          "workspace report leaked absolute project path");
}

}  // namespace

int main() {
    try {
        e2e_contract();
        std::cout << "Project workspace v0.5 end-to-end integration passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Project workspace v0.5 E2E failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
