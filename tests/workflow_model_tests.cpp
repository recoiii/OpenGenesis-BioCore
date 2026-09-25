#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "biocore/domain/workflow.hpp"

namespace {

using namespace biocore::domain;

template <typename Function>
[[nodiscard]] bool rejects(Function&& function) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

[[nodiscard]] WorkflowNode make_node(
    const std::string& id,
    const std::string& label,
    const std::string& module,
    std::vector<WorkflowInputDeclaration> inputs,
    std::vector<WorkflowOutputDeclaration> outputs
) {
    return WorkflowNode{
        WorkflowNodeId{id},
        label,
        module,
        "0.3.0",
        std::move(inputs),
        std::move(outputs),
        {},
    };
}

[[nodiscard]] bool identity_contract() {
    const WorkflowId first{"wf-germline-001"};
    const WorkflowId second{"wf-germline-001"};
    const WorkflowNodeId node{"alignment"};
    return first == second && first.value() == "wf-germline-001" &&
           node.value() == "alignment" &&
           rejects([] { static_cast<void>(WorkflowId{"Bad Workflow"}); }) &&
           rejects([] { static_cast<void>(WorkflowNodeId{"-bad"}); });
}

[[nodiscard]] bool graph_representation_contract() {
    std::vector<WorkflowNode> nodes;
    nodes.push_back(make_node(
        "alignment", "Alignment", "org.biocore.align",
        {WorkflowInputDeclaration{"reads", "fastq", true}},
        {WorkflowOutputDeclaration{"alignment", "bam"}}
    ));
    nodes.push_back(make_node(
        "alignment-qc", "Alignment QC", "org.biocore.alignmentqc",
        {WorkflowInputDeclaration{"alignment", "bam", true}},
        {WorkflowOutputDeclaration{"report", "json"}}
    ));
    nodes.push_back(make_node(
        "coverage", "Coverage", "org.biocore.coverage",
        {WorkflowInputDeclaration{"alignment", "bam", true}},
        {WorkflowOutputDeclaration{"coverage", "tsv"}}
    ));
    nodes.push_back(make_node(
        "summary", "Summary", "org.biocore.report",
        {
            WorkflowInputDeclaration{"qc", "json", true},
            WorkflowInputDeclaration{"coverage", "tsv", true},
        },
        {WorkflowOutputDeclaration{"report", "html"}}
    ));

    const Workflow workflow{
        Workflow::current_schema_version,
        WorkflowId{"wf-branch-merge"},
        "Branch and merge representation",
        "Structural graph only",
        std::move(nodes),
        {
            WorkflowEdge{WorkflowNodeId{"alignment"}, "alignment", WorkflowNodeId{"alignment-qc"}, "alignment"},
            WorkflowEdge{WorkflowNodeId{"alignment"}, "alignment", WorkflowNodeId{"coverage"}, "alignment"},
            WorkflowEdge{WorkflowNodeId{"alignment-qc"}, "report", WorkflowNodeId{"summary"}, "qc"},
            WorkflowEdge{WorkflowNodeId{"coverage"}, "coverage", WorkflowNodeId{"summary"}, "coverage"},
        },
    };

    return workflow.nodes().size() == 4U && workflow.edges().size() == 4U &&
           workflow.edges()[0].source_node_id().value() == "alignment" &&
           workflow.edges()[3].target_node_id().value() == "summary";
}

[[nodiscard]] bool iteration_boundary_contract() {
    const Workflow workflow{
        Workflow::current_schema_version,
        WorkflowId{"wf-cycle-representation"},
        "Cycle representation",
        "",
        {
            make_node(
                "a", "A", "org.biocore.demo.a",
                {WorkflowInputDeclaration{"input", "txt", false}},
                {WorkflowOutputDeclaration{"output", "txt"}}
            ),
            make_node(
                "b", "B", "org.biocore.demo.b",
                {WorkflowInputDeclaration{"input", "txt", false}},
                {WorkflowOutputDeclaration{"output", "txt"}}
            ),
        },
        {
            WorkflowEdge{WorkflowNodeId{"a"}, "output", WorkflowNodeId{"b"}, "input"},
            WorkflowEdge{WorkflowNodeId{"b"}, "output", WorkflowNodeId{"a"}, "input"},
        },
    };

    const Workflow dangling{
        Workflow::current_schema_version,
        WorkflowId{"wf-dangling-representation"},
        "Dangling edge representation",
        "",
        {},
        {
            WorkflowEdge{WorkflowNodeId{"future-source"}, "output", WorkflowNodeId{"future-target"}, "input"},
        },
    };

    return workflow.edges().size() == 2U && dangling.edges().size() == 1U;
}

[[nodiscard]] bool structural_invariant_contract() {
    return rejects([] {
               static_cast<void>(WorkflowInputDeclaration{"Bad", "fastq", true});
           }) &&
           rejects([] {
               static_cast<void>(WorkflowOutputDeclaration{"output", ""});
           }) &&
           rejects([] {
               static_cast<void>(WorkflowNode{WorkflowNodeId{"node"}, "Node", "bad module", "0.3.0"});
           }) &&
           rejects([] {
               static_cast<void>(WorkflowNode{
                   WorkflowNodeId{"node"}, "Node", "org.biocore.demo", "0.3.0",
                   {
                       WorkflowInputDeclaration{"reads", "fastq", true},
                       WorkflowInputDeclaration{"reads", "fastq", false},
                   }
               });
           }) &&
           rejects([] {
               static_cast<void>(Workflow{
                   Workflow::current_schema_version,
                   WorkflowId{"wf-duplicate"},
                   "Duplicate",
                   "",
                   {
                       WorkflowNode{WorkflowNodeId{"node"}, "A", "org.biocore.demo.a", "0.3.0"},
                       WorkflowNode{WorkflowNodeId{"node"}, "B", "org.biocore.demo.b", "0.3.0"},
                   },
                   {}
               });
           }) &&
           rejects([] {
               static_cast<void>(Workflow{2U, WorkflowId{"wf-version"}, "Unsupported", "", {}, {}});
           });
}

}  // namespace

int main() {
    const bool passed =
        identity_contract() &&
        graph_representation_contract() &&
        iteration_boundary_contract() &&
        structural_invariant_contract();

    if (!passed) {
        std::cerr << "Workflow model tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Workflow model tests passed\n";
    return EXIT_SUCCESS;
}
