#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "biocore/domain/workflow.hpp"
#include "biocore/domain/workflow_template.hpp"
#include "biocore/pipeline_protocol/workflow_template_document_codec.hpp"

namespace {

using namespace biocore;

[[nodiscard]] domain::WorkflowTemplate value() {
    return domain::WorkflowTemplate{
        1U,
        "org.biocore.template.demo",
        "1.0.0",
        "Demo",
        "Template description",
        domain::Workflow{
            1U,
            domain::WorkflowId{"org.biocore.template.demo"},
            "Blueprint",
            "Blueprint description",
            {
                domain::WorkflowNode{
                    domain::WorkflowNodeId{"step"},
                    "Step",
                    "org.biocore.test.step",
                    "2.1.0",
                    {},
                    {domain::WorkflowOutputDeclaration{"result", "txt"}},
                    {{"mode", "fast"}}
                }
            },
            {}
        }
    };
}

[[nodiscard]] bool rejects(const std::string_view json) {
    try {
        static_cast<void>(
            pipeline_protocol::parse_workflow_template_document(json)
        );
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

[[nodiscard]] bool round_trip_contract() {
    const auto original = value();
    const std::string encoded =
        pipeline_protocol::serialize_workflow_template_document(original);
    const auto decoded =
        pipeline_protocol::parse_workflow_template_document(encoded);
    return decoded.id() == original.id() &&
           decoded.version() == original.version() &&
           decoded.name() == original.name() &&
           decoded.description() == original.description() &&
           decoded.blueprint().id() == original.blueprint().id() &&
           decoded.blueprint().nodes().size() == 1U &&
           decoded.blueprint().nodes()[0].plugin_version() == "2.1.0";
}

[[nodiscard]] bool deterministic_contract() {
    const auto template_value = value();
    const std::string first =
        pipeline_protocol::serialize_workflow_template_document(template_value);
    const std::string second =
        pipeline_protocol::serialize_workflow_template_document(template_value);
    const std::string third =
        pipeline_protocol::serialize_workflow_template_document(
            pipeline_protocol::parse_workflow_template_document(first)
        );
    return first == second && first == third;
}

[[nodiscard]] bool invalid_contract() {
    const std::string encoded =
        pipeline_protocol::serialize_workflow_template_document(value());

    std::string unknown = encoded;
    unknown.insert(unknown.size() - 1U, ",\"unknown\":\"x\"");

    std::string duplicate = encoded;
    const std::string needle = "\"schemaVersion\":1";
    const auto position = duplicate.find(needle);
    if (position == std::string::npos) return false;
    duplicate.replace(
        position,
        needle.size(),
        "\"schemaVersion\":1,\"schemaVersion\":1"
    );

    std::string bad_schema = encoded;
    const auto schema_position = bad_schema.find(needle);
    if (schema_position == std::string::npos) return false;
    bad_schema.replace(
        schema_position,
        needle.size(),
        "\"schemaVersion\":2"
    );

    const std::string bad_workflow =
        R"({"schemaVersion":1,"id":"org.biocore.template.demo","version":"1.0.0","name":"Demo","description":"","workflowDocument":"{}"})";

    return rejects(unknown) &&
           rejects(duplicate) &&
           rejects(bad_schema) &&
           rejects(bad_workflow);
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) return EXIT_FAILURE;
    const std::string_view name{argv[1]};
    const bool passed =
        (name == "round-trip" && round_trip_contract()) ||
        (name == "deterministic" && deterministic_contract()) ||
        (name == "invalid" && invalid_contract());
    if (!passed) {
        std::cerr << "Workflow template codec test failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Workflow template codec test passed\n";
    return EXIT_SUCCESS;
}
