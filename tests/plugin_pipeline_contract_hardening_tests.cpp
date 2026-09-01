#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_plugin_registry.hpp"
#include "biocore/application/pipeline_planner.hpp"
#include "biocore/domain/pipeline_definition.hpp"
#include "biocore/domain/pipeline_step.hpp"
#include "biocore/pipeline_protocol/pipeline_document.hpp"
#include "biocore/pipeline_protocol/pipeline_document_codec.hpp"

namespace {

class PinnedRegistry final : public biocore::application::IPluginRegistry {
public:
    std::string version{"0.1.0"};
    std::uint32_t manifest_version{2U};
    std::string api_version{"1.0"};

    [[nodiscard]] std::optional<biocore::application::ResolvedPluginModule> find_module(
        const std::string_view module_id
    ) const override {
        if (module_id != "org.biocore.demo.validate") return std::nullopt;
        return biocore::application::ResolvedPluginModule{
            .plugin_id = "org.biocore.demo",
            .plugin_version = version,
            .plugin_manifest_version = manifest_version,
            .plugin_api_version = api_version,
            .module_id = "org.biocore.demo.validate",
            .module_type = biocore::domain::PluginModuleType::process,
            .plugin_root_path = "/plugins/org.biocore.demo",
            .executable_path = "/plugins/org.biocore.demo/bin/demo",
            .parameters = {}, .inputs = {}, .outputs = {},
        };
    }

    [[nodiscard]] std::vector<biocore::application::RegisteredPlugin> list_plugins() const override {
        return {};
    }
};

template <typename Function>
[[nodiscard]] bool rejects(Function&& function) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

[[nodiscard]] biocore::domain::PipelineDefinition definition(std::string plugin_version) {
    return biocore::domain::PipelineDefinition{
        2U,
        "org.biocore.demo.validation",
        "Pinned plugin validation",
        "0.2.0",
        {biocore::domain::PipelineStep{
            "validate", "org.biocore.demo.validate", std::move(plugin_version), {}, 1.0
        }},
    };
}

[[nodiscard]] bool exact_pin_contract() {
    PinnedRegistry registry;
    const auto plan = biocore::application::PipelinePlanner::create_execution_plan(
        definition("0.1.0"), "job-contract", 3, registry
    );
    return plan.schema_version() == 4U && plan.steps().size() == 1U &&
           plan.steps()[0].plugin_version == "0.1.0" &&
           plan.steps()[0].plugin_manifest_version == 2U &&
           plan.steps()[0].plugin_api_version == "1.0";
}

[[nodiscard]] bool mismatch_rejection_contract() {
    PinnedRegistry registry;
    const bool version_mismatch = rejects([&] {
        static_cast<void>(biocore::application::PipelinePlanner::create_execution_plan(
            definition("0.2.0"), "job-version-mismatch", 0, registry
        ));
    });
    registry.api_version = "2.0";
    const bool api_mismatch = rejects([&] {
        static_cast<void>(biocore::application::PipelinePlanner::create_execution_plan(
            definition("0.1.0"), "job-api-mismatch", 0, registry
        ));
    });
    registry.api_version = "1.0";
    registry.manifest_version = 99U;
    const bool manifest_mismatch = rejects([&] {
        static_cast<void>(biocore::application::PipelinePlanner::create_execution_plan(
            definition("0.1.0"), "job-manifest-mismatch", 0, registry
        ));
    });
    return version_mismatch && api_mismatch && manifest_mismatch;
}

[[nodiscard]] bool identity_contract() {
    return rejects([] {
               static_cast<void>(biocore::domain::PipelineDefinition{
                   2U, "Bad Pipeline", "Bad", "1.0.0",
                   {biocore::domain::PipelineStep{"a", "org.biocore.demo.validate", "0.1.0", {}, 1.0}}
               });
           }) &&
           rejects([] {
               static_cast<void>(biocore::domain::PipelineDefinition{
                   2U, "org.biocore.bad", "Bad", "1",
                   {biocore::domain::PipelineStep{"a", "org.biocore.demo.validate", "0.1.0", {}, 1.0}}
               });
           }) &&
           rejects([] {
               static_cast<void>(biocore::domain::PipelineStep{
                   "a", "not a module", "0.1.0", {}, 1.0
               });
           });
}

[[nodiscard]] bool document_round_trip_contract() {
    using namespace biocore::pipeline_protocol;
    const PipelineDefinitionDocument definition_document{
        .schema_version = current_pipeline_definition_schema_version,
        .id = "org.biocore.demo.validation",
        .name = "Pinned",
        .version = "0.2.0",
        .steps = {PipelineStepDocument{
            "validate", "org.biocore.demo.validate", "0.1.0", {}, 1.0
        }},
    };
    const auto parsed_definition = parse_pipeline_definition_document(
        serialize_pipeline_definition_document(definition_document)
    );
    if (parsed_definition.steps[0].plugin_version != "0.1.0") return false;

    const ExecutionPlanDocument plan_document{
        .schema_version = current_execution_plan_schema_version,
        .job_id = "job-doc",
        .job_revision = 1,
        .pipeline_id = "org.biocore.demo.validation",
        .pipeline_version = "0.2.0",
        .steps = {ExecutionPlanStepDocument{
            .id = "validate",
            .module_id = "org.biocore.demo.validate",
            .plugin_id = "org.biocore.demo",
            .plugin_version = "0.1.0",
            .plugin_manifest_version = 2U,
            .plugin_api_version = "1.0",
            .module_type = "process",
            .plugin_root_path = "/plugins/org.biocore.demo",
            .executable_path = "/plugins/org.biocore.demo/bin/demo",
            .depends_on = {},
            .weight = 1.0,
            .parameters = {}, .inputs = {}, .outputs = {},
        }},
    };
    const auto parsed_plan = parse_execution_plan_document(
        serialize_execution_plan_document(plan_document)
    );
    return parsed_plan.steps[0].plugin_manifest_version == 2U &&
           parsed_plan.steps[0].plugin_api_version == "1.0";
}

}  // namespace

int main() {
    if (!exact_pin_contract() || !mismatch_rejection_contract() ||
        !identity_contract() || !document_round_trip_contract()) {
        std::cerr << "Plugin/pipeline contract hardening tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Plugin/pipeline contract hardening tests passed\n";
    return EXIT_SUCCESS;
}
