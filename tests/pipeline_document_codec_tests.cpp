#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "biocore/pipeline_protocol/pipeline_document.hpp"
#include "biocore/pipeline_protocol/pipeline_document_codec.hpp"

namespace {

using namespace biocore::pipeline_protocol;

template <typename Function>
[[nodiscard]] bool rejects(Function&& function) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

[[nodiscard]] PipelineDefinitionDocument definition_document() {
    return PipelineDefinitionDocument{
        .schema_version = 1U,
        .id = "org.biocore.demo",
        .name = "Demo \"pipeline\"",
        .version = "1.0.0",
        .steps = {
            PipelineStepDocument{"validate", "org.biocore.demo.validate", {}, 0.2},
            PipelineStepDocument{"scan", "org.biocore.demo.scan", {"validate"}, 0.8},
        },
    };
}

[[nodiscard]] bool round_trip_contract() {
    const auto definition = definition_document();
    const std::string encoded = serialize_pipeline_definition_document(definition);
    const auto decoded = parse_pipeline_definition_document(encoded);
    if (decoded.id != definition.id || decoded.name != definition.name ||
        decoded.steps.size() != 2U || decoded.steps[1].depends_on != std::vector<std::string>{"validate"}) {
        return false;
    }

    const ExecutionPlanDocument plan{
        .schema_version = current_execution_plan_schema_version,
        .job_id = "job-1",
        .job_revision = 4,
        .pipeline_id = definition.id,
        .pipeline_version = definition.version,
        .steps = {
            ExecutionPlanStepDocument{
                .id = "validate",
                .module_id = "org.biocore.demo.validate",
                .plugin_id = "org.biocore.demo",
                .plugin_version = "0.1.0",
                .module_type = "process",
                .plugin_root_path = "/plugins/org.biocore.demo",
                .executable_path = "/plugins/org.biocore.demo/bin/demo",
                .depends_on = {},
                .weight = 0.2,
                .parameters = {}, .inputs = {}, .outputs = {},
            },
            ExecutionPlanStepDocument{
                .id = "scan",
                .module_id = "org.biocore.demo.scan",
                .plugin_id = "org.biocore.demo",
                .plugin_version = "0.1.0",
                .module_type = "process",
                .plugin_root_path = "/plugins/org.biocore.demo",
                .executable_path = "/plugins/org.biocore.demo/bin/demo",
                .depends_on = {"validate"},
                .weight = 0.8,
                .parameters = {}, .inputs = {}, .outputs = {},
            },
        },
    };
    const auto decoded_plan = parse_execution_plan_document(
        serialize_execution_plan_document(plan)
    );
    return decoded_plan.job_id == "job-1" && decoded_plan.job_revision == 4 &&
           decoded_plan.steps[0].module_id == "org.biocore.demo.validate";
}

[[nodiscard]] bool strict_schema_contract() {
    return rejects([] {
               static_cast<void>(parse_pipeline_definition_document(
                   R"({"schemaVersion":1,"id":"p","name":"P","version":"1","unknown":1,"steps":[{"id":"a","module":"m","dependsOn":[],"weight":1}]})"
               ));
           }) &&
           rejects([] {
               static_cast<void>(parse_pipeline_definition_document(
                   R"({"schemaVersion":1,"id":"p","id":"q","name":"P","version":"1","steps":[{"id":"a","module":"m","dependsOn":[],"weight":1}]})"
               ));
           }) &&
           rejects([] {
               static_cast<void>(parse_execution_plan_document(
                   R"({"schemaVersion":2,"jobId":"j","jobRevision":0,"pipelineId":"p","pipelineVersion":"1","steps":[{"id":"a","module":"m","dependsOn":[],"weight":1}]})"
               ));
           }) &&
           rejects([] {
               std::string invalid = R"({"schemaVersion":1,"id":")";
               invalid.push_back(static_cast<char>(0xFF));
               invalid += R"(","name":"P","version":"1","steps":[]})";
               static_cast<void>(parse_pipeline_definition_document(invalid));
           }) &&
           rejects([] {
               auto document = definition_document();
               document.name = std::string{"bad\0name", 8U};
               static_cast<void>(serialize_pipeline_definition_document(document));
           });
}

}  // namespace

int main() {
    if (!round_trip_contract() || !strict_schema_contract()) {
        std::cerr << "Pipeline document codec tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Pipeline document codec tests passed\n";
    return EXIT_SUCCESS;
}
