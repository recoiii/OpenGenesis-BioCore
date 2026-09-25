#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "biocore/domain/workflow.hpp"
#include "biocore/pipeline_protocol/workflow_document_codec.hpp"

namespace {

using namespace biocore::domain;
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

[[nodiscard]] Workflow sample_workflow() {
    return Workflow{
        Workflow::current_schema_version,
        WorkflowId{"wf-germline-001"},
        "Germline workflow",
        "FASTQ to annotation",
        {
            WorkflowNode{
                WorkflowNodeId{"qc"},
                "FASTQ QC",
                "org.biocore.fastqqc",
                "0.3.0",
                {WorkflowInputDeclaration{"reads", "fastq", true}},
                {WorkflowOutputDeclaration{"report", "json"}},
                {{"min-quality", "20"}, {"mode", "strict"}},
            },
            WorkflowNode{
                WorkflowNodeId{"alignment"},
                "Alignment",
                "org.biocore.align",
                "0.3.0",
                {WorkflowInputDeclaration{"reads", "fastq", true}},
                {WorkflowOutputDeclaration{"alignment", "bam"}},
                {},
            },
        },
        {
            WorkflowEdge{WorkflowNodeId{"qc"}, "report", WorkflowNodeId{"alignment"}, "reads"},
        },
    };
}

[[nodiscard]] bool round_trip_contract() {
    const Workflow original = sample_workflow();
    const std::string encoded = serialize_workflow_document(original);
    const Workflow decoded = parse_workflow_document(encoded);

    return decoded.schema_version() == Workflow::current_schema_version &&
           decoded.id() == original.id() &&
           decoded.name() == original.name() &&
           decoded.description() == original.description() &&
           decoded.nodes().size() == 2U &&
           decoded.nodes()[0].id() == original.nodes()[0].id() &&
           decoded.nodes()[0].parameters().at("min-quality") == "20" &&
           decoded.nodes()[1].outputs()[0].artifact_type() == "bam" &&
           decoded.edges().size() == 1U &&
           decoded.edges()[0].source_node_id().value() == "qc" &&
           decoded.edges()[0].target_node_id().value() == "alignment";
}

[[nodiscard]] bool deterministic_serialization_contract() {
    const Workflow workflow = sample_workflow();
    const std::string first = serialize_workflow_document(workflow);
    const std::string second = serialize_workflow_document(workflow);
    const std::string third = serialize_workflow_document(parse_workflow_document(first));
    return first == second && first == third &&
           first.find("\"min-quality\":\"20\",\"mode\":\"strict\"") != std::string::npos;
}

[[nodiscard]] bool strict_schema_contract() {
    return rejects([] {
               static_cast<void>(parse_workflow_document(
                   R"({"schemaVersion":2,"id":"wf-x","name":"X","nodes":[],"edges":[]})"
               ));
           }) &&
           rejects([] {
               static_cast<void>(parse_workflow_document(
                   R"({"schemaVersion":1,"name":"X","nodes":[],"edges":[]})"
               ));
           }) &&
           rejects([] {
               static_cast<void>(parse_workflow_document(
                   R"({"schemaVersion":1,"id":"wf-x","id":"wf-y","name":"X","nodes":[],"edges":[]})"
               ));
           }) &&
           rejects([] {
               static_cast<void>(parse_workflow_document(
                   R"({"schemaVersion":1,"id":"wf-x","name":"X","unknown":1,"nodes":[],"edges":[]})"
               ));
           }) &&
           rejects([] {
               static_cast<void>(parse_workflow_document(
                   R"({"schemaVersion":1,"id":"wf-x","name":"X","nodes":[],"edges":[{"sourceNode":"a","sourceOutput":"out","targetNode":"b"}]})"
               ));
           }) &&
           rejects([] {
               static_cast<void>(parse_workflow_document(
                   R"({"schemaVersion":1,"id":"wf-x","name":"X","nodes":[{"id":"a","label":"A","moduleId":"org.biocore.demo","pluginVersion":"0.3.0","inputs":[],"outputs":[],"parameters":{"x":1}}],"edges":[]})"
               ));
           });
}

[[nodiscard]] bool optional_description_and_graph_boundary_contract() {
    const Workflow decoded = parse_workflow_document(
        R"({"schemaVersion":1,"id":"wf-cycle","name":"Cycle","nodes":[{"id":"a","label":"A","moduleId":"org.biocore.demo.a","pluginVersion":"0.3.0","inputs":[{"name":"input","artifactType":"txt","required":false}],"outputs":[{"name":"output","artifactType":"txt"}],"parameters":{}},{"id":"b","label":"B","moduleId":"org.biocore.demo.b","pluginVersion":"0.3.0","inputs":[{"name":"input","artifactType":"txt","required":false}],"outputs":[{"name":"output","artifactType":"txt"}],"parameters":{}}],"edges":[{"sourceNode":"a","sourceOutput":"output","targetNode":"b","targetInput":"input"},{"sourceNode":"b","sourceOutput":"output","targetNode":"a","targetInput":"input"}]})"
    );

    return decoded.description().empty() && decoded.edges().size() == 2U;
}

}  // namespace

int main() {
    const bool passed =
        round_trip_contract() &&
        deterministic_serialization_contract() &&
        strict_schema_contract() &&
        optional_description_and_graph_boundary_contract();

    if (!passed) {
        std::cerr << "Workflow document codec tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Workflow document codec tests passed\n";
    return EXIT_SUCCESS;
}
