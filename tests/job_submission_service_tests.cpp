#include <cstdlib>
#include <deque>
#include <iostream>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_execution_plan_store.hpp"
#include "biocore/application/i_id_generator.hpp"
#include "biocore/application/i_managed_file_repository.hpp"
#include "biocore/application/i_pipeline_catalog.hpp"
#include "biocore/application/i_plugin_registry.hpp"
#include "biocore/application/i_prepared_job_store.hpp"
#include "biocore/application/i_utc_clock.hpp"
#include "biocore/application/job_submission_service.hpp"
#include "biocore/application/job_submission_service_error.hpp"
#include "biocore/application/pipeline_bindings.hpp"
#include "biocore/application/pipeline_preparation_service.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/pipeline_definition.hpp"
#include "biocore/domain/plugin_io_contract.hpp"
#include "biocore/domain/storage_mode.hpp"
#include "biocore/domain/pipeline_step.hpp"
#include "plugin_test_support.hpp"

namespace {

using namespace biocore;

class Catalog final : public application::IPipelineCatalog {
public:
    std::optional<domain::PipelineDefinition> find(std::string_view id, std::string_view version) const override {
        if (id != "org.biocore.demo.validation" || version != "0.1.0") return std::nullopt;
        return domain::PipelineDefinition{1U, "org.biocore.demo.validation", "Demo", "0.1.0", {
            {"validate", "org.biocore.demo.validate", {}, 1.0},
        }};
    }
    std::vector<application::RegisteredPipeline> list() const override { return {}; }
};


class BindingCatalog final : public application::IPipelineCatalog {
public:
    std::optional<domain::PipelineDefinition> find(
        const std::string_view id,
        const std::string_view version
    ) const override {
        if (id != "org.biocore.io" || version != "1.0.0") return std::nullopt;
        return domain::PipelineDefinition{
            1U,
            "org.biocore.io",
            "I/O binding test",
            "1.0.0",
            {domain::PipelineStep{"copy", "org.biocore.demo.copy", {}, 1.0}},
        };
    }

    std::vector<application::RegisteredPipeline> list() const override { return {}; }
};

class BindingRegistry final : public application::IPluginRegistry {
public:
    std::optional<application::ResolvedPluginModule> find_module(
        const std::string_view id
    ) const override {
        if (id != "org.biocore.demo.copy") return std::nullopt;
        return application::ResolvedPluginModule{
            .plugin_id = "org.biocore.demo",
            .plugin_version = "0.1.0",
            .module_id = std::string{id},
            .module_type = domain::PluginModuleType::process,
            .plugin_root_path = "/plugins/demo",
            .executable_path = "/plugins/demo/bin/demo",
            .parameters = {
                domain::PluginParameterDefinition{
                    "label",
                    domain::PluginParameterType::string,
                    false,
                    domain::PluginParameterValue{std::string{"demo"}}
                },
                domain::PluginParameterDefinition{
                    "repeat",
                    domain::PluginParameterType::integer,
                    false,
                    domain::PluginParameterValue{std::int64_t{1}},
                    1.0,
                    3.0
                },
            },
            .inputs = {domain::PluginInputPortDefinition{"source", true, {"txt"}}},
            .outputs = {domain::PluginOutputPortDefinition{"result", "txt"}},
        };
    }

    std::vector<application::RegisteredPlugin> list_plugins() const override { return {}; }
};

class BindingFiles final : public application::IManagedFileRepository {
public:
    domain::ManagedFile file{
        "file-1",
        "source.txt",
        domain::StorageMode::managed_copy,
        std::string{"/original/source.txt"},
        std::string{"/project/inputs/file-1/source.txt"},
        std::string{"inputs/file-1/source.txt"},
        "txt",
        4,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        "2026-08-07T00:00:00Z",
        "2026-08-07T00:00:00Z"
    };

    bool add(const domain::ManagedFile&) override { return false; }

    std::optional<domain::ManagedFile> find_by_id(const std::string_view id) override {
        return id == file.id() ? std::optional<domain::ManagedFile>{file} : std::nullopt;
    }

    std::optional<domain::ManagedFile> find_by_relative_project_path(
        std::string_view
    ) override {
        return std::nullopt;
    }

    std::vector<domain::ManagedFile> list() override { return {file}; }

    bool add_generated_output(
        const domain::ManagedFile&,
        const application::GeneratedOutputProvenance&
    ) override {
        return false;
    }

    bool add_generated_outputs_batch(
        std::span<const application::GeneratedOutputArtifact>
    ) override {
        return false;
    }

    std::optional<application::GeneratedOutputArtifact> find_generated_output(
        std::string_view,
        std::string_view,
        std::string_view
    ) override {
        return std::nullopt;
    }

    std::vector<application::GeneratedOutputArtifact> list_generated_outputs(
        std::string_view
    ) override {
        return {};
    }
};

class Store final : public application::IExecutionPlanStore {
public:
    std::string store(const application::ExecutionPlan& plan) override {
        revisions.push_back(plan.job_revision());
        paths.push_back("/tmp/" + std::string{plan.job_id()} + "-r" + std::to_string(plan.job_revision()) + ".json");
        return paths.back();
    }
    void discard(std::string_view path) override { discarded.emplace_back(path); }
    std::vector<std::int64_t> revisions;
    std::vector<std::string> paths;
    std::vector<std::string> discarded;
};

class PreparedStore final : public application::IPreparedJobStore {
public:
    bool add_prepared_job(const domain::Job& job, const application::PreparedJobExecution& execution) override {
        jobs.push_back(job);
        executions.push_back(execution);
        if (fail_first && jobs.size() == 1U) return false;
        return true;
    }
    std::optional<application::PreparedJobExecution> find_execution(std::string_view) override { return std::nullopt; }
    bool fail_first{false};
    std::vector<domain::Job> jobs;
    std::vector<application::PreparedJobExecution> executions;
};

class Ids final : public application::IIdGenerator {
public:
    explicit Ids(std::deque<std::string> values) : values_{std::move(values)} {}
    std::string generate() override { auto value = values_.front(); values_.pop_front(); return value; }
private:
    std::deque<std::string> values_;
};

class Clock final : public application::IUtcClock {
public:
    std::string now_utc_iso8601() override { return "2026-08-07T12:00:" + std::to_string(counter_++) + "Z"; }
private:
    int counter_{10};
};

void require(bool condition, std::string_view message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}


template <typename Function>
[[nodiscard]] bool rejects_invalid_argument(Function&& function) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

}  // namespace

int main() {
    Catalog catalog;
    tests::FakePluginRegistry plugins;
    Store snapshots;
    PreparedStore prepared;
    Ids ids{{"job-ready"}};
    Clock clock;
    application::PipelinePreparationService preparation{snapshots, plugins};
    application::JobSubmissionService service{prepared, catalog, preparation, snapshots, ids, clock};
    const auto job = service.submit({
        .analysis_id = std::string{"analysis-a"},
        .pipeline_id = "org.biocore.demo.validation",
        .pipeline_version = "0.1.0",
        .priority = domain::JobPriority::high,
        .bindings = {},
    });
    require(job.status() == domain::JobStatus::queued && job.revision() == 1, "submission must return queued r1 job");
    require(snapshots.revisions == std::vector<std::int64_t>{2}, "snapshot must target preparing launch revision r2");
    require(prepared.executions.size() == 1U && prepared.executions[0].launch_revision == 2, "prepared association launch revision");
    require(prepared.executions[0].execution_plan_path == snapshots.paths[0], "prepared association path");
    require(snapshots.discarded.empty(), "successful snapshot must remain published");

    Store collision_snapshots;
    PreparedStore collision_prepared;
    collision_prepared.fail_first = true;
    Ids collision_ids{{"job-collision", "job-next"}};
    application::PipelinePreparationService collision_preparation{collision_snapshots, plugins};
    application::JobSubmissionService collision_service{collision_prepared, catalog, collision_preparation, collision_snapshots, collision_ids, clock};
    const auto retried = collision_service.submit({std::nullopt, "org.biocore.demo.validation", "0.1.0", domain::JobPriority::normal, {}});
    require(retried.id() == "job-next", "identifier collision must retry");
    require(collision_snapshots.discarded.size() == 1U && collision_snapshots.discarded[0].find("job-collision") != std::string::npos,
            "collision snapshot must be discarded");

    bool missing = false;
    try {
        static_cast<void>(service.submit({std::nullopt, "missing", "1.0", domain::JobPriority::normal, {}}));
    } catch (const application::JobSubmissionError& error) {
        missing = error.code() == application::JobSubmissionErrorCode::pipeline_not_found;
    }
    require(missing, "missing pipeline must be typed failure");


    BindingCatalog binding_catalog;
    BindingRegistry binding_plugins;
    BindingFiles binding_files;
    Store binding_snapshots;
    PreparedStore binding_prepared;
    Ids binding_ids{{
        "job-bind-ok",
        "job-bind-type",
        "job-bind-range",
        "job-bind-required",
        "job-bind-missing-file"
    }};
    application::PipelinePreparationService binding_preparation{
        binding_snapshots, binding_plugins, binding_files
    };
    application::JobSubmissionService binding_service{
        binding_prepared,
        binding_catalog,
        binding_preparation,
        binding_snapshots,
        binding_ids,
        clock
    };

    const application::PipelineRunBindings valid_bindings{{
        application::PipelineStepBindings{
            .step_id = "copy",
            .parameters = {
                {"label", domain::PluginParameterValue{std::string{"hello"}}},
                {"repeat", domain::PluginParameterValue{std::int64_t{2}}},
            },
            .inputs = {
                {"source", application::ManagedFileInputSource{"file-1"}},
            },
        },
    }};
    const auto bound_job = binding_service.submit({
        .analysis_id = std::nullopt,
        .pipeline_id = "org.biocore.io",
        .pipeline_version = "1.0.0",
        .priority = domain::JobPriority::normal,
        .bindings = valid_bindings,
    });
    require(bound_job.status() == domain::JobStatus::queued,
            "valid REST-equivalent bindings must prepare a queued job");
    require(binding_prepared.executions.size() == 1U,
            "valid bindings must persist one prepared association");

    require(rejects_invalid_argument([&] {
        application::PipelineRunBindings bindings{{
            application::PipelineStepBindings{
                .step_id = "copy",
                .parameters = {
                    {"repeat", domain::PluginParameterValue{std::string{"2"}}},
                },
                .inputs = {
                    {"source", application::ManagedFileInputSource{"file-1"}},
                },
            },
        }};
        static_cast<void>(binding_service.submit({
            std::nullopt, "org.biocore.io", "1.0.0",
            domain::JobPriority::normal, std::move(bindings)
        }));
    }), "parameter type mismatch must fail before prepared persistence");

    require(rejects_invalid_argument([&] {
        application::PipelineRunBindings bindings{{
            application::PipelineStepBindings{
                .step_id = "copy",
                .parameters = {
                    {"repeat", domain::PluginParameterValue{std::int64_t{4}}},
                },
                .inputs = {
                    {"source", application::ManagedFileInputSource{"file-1"}},
                },
            },
        }};
        static_cast<void>(binding_service.submit({
            std::nullopt, "org.biocore.io", "1.0.0",
            domain::JobPriority::normal, std::move(bindings)
        }));
    }), "parameter schema maximum violation must fail");

    require(rejects_invalid_argument([&] {
        application::PipelineRunBindings bindings{{
            application::PipelineStepBindings{
                .step_id = "copy",
                .parameters = {},
                .inputs = {},
            },
        }};
        static_cast<void>(binding_service.submit({
            std::nullopt, "org.biocore.io", "1.0.0",
            domain::JobPriority::normal, std::move(bindings)
        }));
    }), "missing required managed-file input must fail");

    require(rejects_invalid_argument([&] {
        application::PipelineRunBindings bindings{{
            application::PipelineStepBindings{
                .step_id = "copy",
                .parameters = {},
                .inputs = {
                    {"source", application::ManagedFileInputSource{"missing-file"}},
                },
            },
        }};
        static_cast<void>(binding_service.submit({
            std::nullopt, "org.biocore.io", "1.0.0",
            domain::JobPriority::normal, std::move(bindings)
        }));
    }), "unknown managed-file id must fail");

    require(binding_prepared.executions.size() == 1U,
            "invalid bindings must not persist prepared associations");
    require(binding_snapshots.revisions.size() == 1U,
            "invalid bindings must fail before execution-plan publication");
    std::cout << "Job submission service tests passed\n";
    return 0;
}
