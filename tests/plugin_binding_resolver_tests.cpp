#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "biocore/application/plugin_binding_resolver.hpp"
#include "biocore/application/pipeline_planner.hpp"
#include "biocore/domain/managed_file.hpp"
#include "biocore/domain/pipeline_definition.hpp"

namespace {
using namespace biocore;

class FileRepo final : public application::IManagedFileRepository {
public:
    domain::ManagedFile file{
        "file-1", "source.txt", domain::StorageMode::managed_copy,
        std::string{"/original/source.txt"}, std::string{"/project/inputs/file-1/source.txt"},
        std::string{"inputs/file-1/source.txt"}, "txt", 4, std::nullopt, std::nullopt,
        std::nullopt, "2026-08-07T00:00:00Z", "2026-08-07T00:00:00Z"
    };
    bool add(const domain::ManagedFile&) override { return false; }
    std::optional<domain::ManagedFile> find_by_id(std::string_view id) override {
        return id == file.id() ? std::optional<domain::ManagedFile>{file} : std::nullopt;
    }
    std::optional<domain::ManagedFile> find_by_relative_project_path(std::string_view) override { return std::nullopt; }
    std::vector<domain::ManagedFile> list() override { return {file}; }
    bool add_generated_output(const domain::ManagedFile&, const application::GeneratedOutputProvenance&) override { return false; }
    bool add_generated_outputs_batch(
        std::span<const biocore::application::GeneratedOutputArtifact>
    ) override { return false; }
    std::optional<application::GeneratedOutputArtifact> find_generated_output(std::string_view, std::string_view, std::string_view) override { return std::nullopt; }
    std::vector<application::GeneratedOutputArtifact> list_generated_outputs(std::string_view) override { return {}; }
};

class Registry final : public application::IPluginRegistry {
public:
    std::optional<application::ResolvedPluginModule> find_module(std::string_view id) const override {
        if (id != "org.biocore.demo.copy") return std::nullopt;
        return application::ResolvedPluginModule{
            .plugin_id="org.biocore.demo", .plugin_version="0.1.0", .module_id=std::string{id},
            .module_type=domain::PluginModuleType::process, .plugin_root_path="/plugins/demo",
            .executable_path="/plugins/demo/bin/demo",
            .parameters={
                domain::PluginParameterDefinition{"label", domain::PluginParameterType::string, false, domain::PluginParameterValue{std::string{"demo"}}},
                domain::PluginParameterDefinition{"repeat", domain::PluginParameterType::integer, false, domain::PluginParameterValue{std::int64_t{1}}, 1.0, 3.0},
            },
            .inputs={domain::PluginInputPortDefinition{"source", true, {"txt"}}},
            .outputs={domain::PluginOutputPortDefinition{"result", "txt"}},
        };
    }
    std::vector<application::RegisteredPlugin> list_plugins() const override { return {}; }
};

template <typename Function>
[[nodiscard]] bool rejects(Function&& fn) {
    try { fn(); } catch (const std::invalid_argument&) { return true; }
    return false;
}

[[nodiscard]] domain::PipelineDefinition definition() {
    return domain::PipelineDefinition{1U,"org.biocore.io","IO","1.0.0",
        {domain::PipelineStep{"copy","org.biocore.demo.copy",{},1.0}}};
}

[[nodiscard]] bool resolution_contract() {
    Registry registry; FileRepo files;
    auto plan = application::PipelinePlanner::create_execution_plan(definition(), "job-1", 2, registry);
    application::PipelineRunBindings bindings{{application::PipelineStepBindings{
        .step_id="copy",
        .parameters={{"label", domain::PluginParameterValue{std::string{"hello"}}}},
        .inputs={{"source", application::ManagedFileInputSource{"file-1"}}},
    }}};
    const auto resolved = application::PluginBindingResolver::resolve(plan, bindings, files);
    const auto& step = resolved.steps().front();
    return step.parameters.size()==2U && step.inputs.size()==1U && step.outputs.size()==1U &&
           step.inputs.front().relative_project_path=="inputs/file-1/source.txt" &&
           step.outputs.front().relative_project_path=="outputs/job-1--copy--result.out";
}

[[nodiscard]] bool rejection_contract() {
    Registry registry; FileRepo files;
    auto plan = application::PipelinePlanner::create_execution_plan(definition(), "job-1", 0, registry);
    return rejects([&] { static_cast<void>(application::PluginBindingResolver::resolve(plan, {}, files)); }) &&
           rejects([&] {
               application::PipelineRunBindings bindings{{application::PipelineStepBindings{
                   .step_id="copy", .parameters={{"unknown", domain::PluginParameterValue{std::string{"x"}}}},
                   .inputs={{"source", application::ManagedFileInputSource{"file-1"}}}
               }}};
               static_cast<void>(application::PluginBindingResolver::resolve(plan, bindings, files));
           });
}
}

int main() {
    if (!resolution_contract() || !rejection_contract()) {
        std::cerr << "Plugin binding resolver tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Plugin binding resolver tests passed\n";
    return EXIT_SUCCESS;
}
