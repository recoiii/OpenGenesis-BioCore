#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/domain/workflow.hpp"
#include "biocore/domain/workflow_dag.hpp"

namespace {

using namespace biocore::domain;

[[nodiscard]] WorkflowNode node(
    std::string id,
    std::vector<WorkflowInputDeclaration> inputs = {},
    std::vector<WorkflowOutputDeclaration> outputs = {}
) {
    const std::string module_id = "org.biocore.test." + id;
    return WorkflowNode{
        WorkflowNodeId{std::move(id)},
        "Test node",
        module_id,
        "0.4.0",
        std::move(inputs),
        std::move(outputs),
        {},
    };
}

[[nodiscard]] bool throws_code(
    const Workflow& workflow,
    const WorkflowDagErrorCode expected
) {
    try {
        static_cast<void>(plan_workflow_dag(workflow));
    } catch (const WorkflowDagError& error) {
        return error.code() == expected &&
               std::string_view{error.what()}.find("Workflow DAG validation failed") !=
                   std::string_view::npos;
    }
    return false;
}

[[nodiscard]] std::vector<std::string> order_values(const WorkflowDagPlan& plan) {
    std::vector<std::string> result;
    result.reserve(plan.ordered_nodes().size());
    for (const WorkflowPlanNode& planned : plan.ordered_nodes()) {
        result.emplace_back(planned.id.value());
    }
    return result;
}

[[nodiscard]] std::vector<std::vector<std::string>> stage_values(
    const WorkflowDagPlan& plan
) {
    std::vector<std::vector<std::string>> result;
    result.reserve(plan.execution_stages().size());
    for (const auto& stage : plan.execution_stages()) {
        std::vector<std::string> values;
        values.reserve(stage.size());
        for (const WorkflowNodeId& id : stage) values.emplace_back(id.value());
        result.push_back(std::move(values));
    }
    return result;
}

[[nodiscard]] Workflow branch_merge_workflow(
    const bool reverse_nodes = false,
    const bool reverse_edges = false
) {
    std::vector<WorkflowNode> nodes{
        node(
            "align",
            {WorkflowInputDeclaration{"reads", "fastq", true}},
            {WorkflowOutputDeclaration{"bam", "bam"}}
        ),
        node(
            "qc",
            {WorkflowInputDeclaration{"bam", "bam", true}},
            {WorkflowOutputDeclaration{"report", "json"}}
        ),
        node(
            "coverage",
            {WorkflowInputDeclaration{"bam", "bam", true}},
            {WorkflowOutputDeclaration{"table", "tsv"}}
        ),
        node(
            "summary",
            {
                WorkflowInputDeclaration{"coverage", "tsv", true},
                WorkflowInputDeclaration{"qc", "json", true},
            },
            {WorkflowOutputDeclaration{"report", "html"}}
        ),
    };
    std::vector<WorkflowEdge> edges{
        WorkflowEdge{WorkflowNodeId{"align"}, "bam", WorkflowNodeId{"qc"}, "bam"},
        WorkflowEdge{WorkflowNodeId{"align"}, "bam", WorkflowNodeId{"coverage"}, "bam"},
        WorkflowEdge{WorkflowNodeId{"qc"}, "report", WorkflowNodeId{"summary"}, "qc"},
        WorkflowEdge{WorkflowNodeId{"coverage"}, "table", WorkflowNodeId{"summary"}, "coverage"},
    };

    if (reverse_nodes) std::reverse(nodes.begin(), nodes.end());
    if (reverse_edges) std::reverse(edges.begin(), edges.end());

    return Workflow{
        Workflow::current_schema_version,
        WorkflowId{"wf-dag-test"},
        "DAG test",
        "",
        std::move(nodes),
        std::move(edges),
    };
}

[[nodiscard]] bool valid_contract() {
    const WorkflowDagPlan plan = plan_workflow_dag(branch_merge_workflow());
    if (order_values(plan) !=
        std::vector<std::string>{"align", "coverage", "qc", "summary"}) {
        return false;
    }
    if (stage_values(plan) !=
        std::vector<std::vector<std::string>>{
            {"align"}, {"coverage", "qc"}, {"summary"}
        }) {
        return false;
    }

    const auto& ordered = plan.ordered_nodes();
    return ordered[0].depends_on.empty() &&
           ordered[1].depends_on == std::vector<WorkflowNodeId>{WorkflowNodeId{"align"}} &&
           ordered[2].depends_on == std::vector<WorkflowNodeId>{WorkflowNodeId{"align"}} &&
           ordered[3].depends_on ==
               std::vector<WorkflowNodeId>{WorkflowNodeId{"coverage"}, WorkflowNodeId{"qc"}};
}

[[nodiscard]] bool deterministic_contract() {
    const WorkflowDagPlan first = plan_workflow_dag(branch_merge_workflow(false, false));
    const WorkflowDagPlan second = plan_workflow_dag(branch_merge_workflow(true, true));
    return order_values(first) == order_values(second) &&
           stage_values(first) == stage_values(second);
}

[[nodiscard]] bool missing_node_contract() {
    const Workflow missing_source{
        Workflow::current_schema_version,
        WorkflowId{"wf-missing-source"},
        "Missing source",
        "",
        {node("target", {WorkflowInputDeclaration{"input", "txt", true}}, {})},
        {
            WorkflowEdge{
                WorkflowNodeId{"missing"},
                "output",
                WorkflowNodeId{"target"},
                "input"
            },
        },
    };

    const Workflow missing_target{
        Workflow::current_schema_version,
        WorkflowId{"wf-missing-target"},
        "Missing target",
        "",
        {node("source", {}, {WorkflowOutputDeclaration{"output", "txt"}})},
        {
            WorkflowEdge{
                WorkflowNodeId{"source"},
                "output",
                WorkflowNodeId{"missing"},
                "input"
            },
        },
    };

    return throws_code(missing_source, WorkflowDagErrorCode::missing_source_node) &&
           throws_code(missing_target, WorkflowDagErrorCode::missing_target_node);
}

[[nodiscard]] bool missing_port_contract() {
    const Workflow missing_output{
        Workflow::current_schema_version,
        WorkflowId{"wf-missing-output"},
        "Missing output",
        "",
        {
            node("source", {}, {WorkflowOutputDeclaration{"other", "txt"}}),
            node("target", {WorkflowInputDeclaration{"input", "txt", true}}, {}),
        },
        {
            WorkflowEdge{
                WorkflowNodeId{"source"},
                "output",
                WorkflowNodeId{"target"},
                "input"
            },
        },
    };

    const Workflow missing_input{
        Workflow::current_schema_version,
        WorkflowId{"wf-missing-input"},
        "Missing input",
        "",
        {
            node("source", {}, {WorkflowOutputDeclaration{"output", "txt"}}),
            node("target", {WorkflowInputDeclaration{"other", "txt", true}}, {}),
        },
        {
            WorkflowEdge{
                WorkflowNodeId{"source"},
                "output",
                WorkflowNodeId{"target"},
                "input"
            },
        },
    };

    return throws_code(missing_output, WorkflowDagErrorCode::missing_source_output) &&
           throws_code(missing_input, WorkflowDagErrorCode::missing_target_input);
}

[[nodiscard]] bool duplicate_contract() {
    const Workflow duplicate_edge{
        Workflow::current_schema_version,
        WorkflowId{"wf-duplicate-edge"},
        "Duplicate edge",
        "",
        {
            node("source", {}, {WorkflowOutputDeclaration{"output", "txt"}}),
            node("target", {WorkflowInputDeclaration{"input", "txt", true}}, {}),
        },
        {
            WorkflowEdge{WorkflowNodeId{"source"}, "output", WorkflowNodeId{"target"}, "input"},
            WorkflowEdge{WorkflowNodeId{"source"}, "output", WorkflowNodeId{"target"}, "input"},
        },
    };

    const Workflow multiple_producers{
        Workflow::current_schema_version,
        WorkflowId{"wf-multiple-producers"},
        "Multiple producers",
        "",
        {
            node("source-a", {}, {WorkflowOutputDeclaration{"output", "txt"}}),
            node("source-b", {}, {WorkflowOutputDeclaration{"output", "txt"}}),
            node("target", {WorkflowInputDeclaration{"input", "txt", true}}, {}),
        },
        {
            WorkflowEdge{WorkflowNodeId{"source-a"}, "output", WorkflowNodeId{"target"}, "input"},
            WorkflowEdge{WorkflowNodeId{"source-b"}, "output", WorkflowNodeId{"target"}, "input"},
        },
    };

    return throws_code(duplicate_edge, WorkflowDagErrorCode::duplicate_edge) &&
           throws_code(
               multiple_producers,
               WorkflowDagErrorCode::multiple_incoming_connections
           );
}

[[nodiscard]] bool self_edge_contract() {
    const Workflow workflow{
        Workflow::current_schema_version,
        WorkflowId{"wf-self-edge"},
        "Self edge",
        "",
        {
            node(
                "self",
                {WorkflowInputDeclaration{"input", "txt", true}},
                {WorkflowOutputDeclaration{"output", "txt"}}
            ),
        },
        {
            WorkflowEdge{WorkflowNodeId{"self"}, "output", WorkflowNodeId{"self"}, "input"},
        },
    };
    return throws_code(workflow, WorkflowDagErrorCode::self_edge);
}

[[nodiscard]] bool cycle_contract() {
    const Workflow workflow{
        Workflow::current_schema_version,
        WorkflowId{"wf-cycle"},
        "Cycle",
        "",
        {
            node(
                "a",
                {WorkflowInputDeclaration{"input", "txt", false}},
                {WorkflowOutputDeclaration{"output", "txt"}}
            ),
            node(
                "b",
                {WorkflowInputDeclaration{"input", "txt", false}},
                {WorkflowOutputDeclaration{"output", "txt"}}
            ),
        },
        {
            WorkflowEdge{WorkflowNodeId{"a"}, "output", WorkflowNodeId{"b"}, "input"},
            WorkflowEdge{WorkflowNodeId{"b"}, "output", WorkflowNodeId{"a"}, "input"},
        },
    };

    const Workflow empty{
        Workflow::current_schema_version,
        WorkflowId{"wf-empty"},
        "Empty",
        "",
        {},
        {},
    };

    return throws_code(workflow, WorkflowDagErrorCode::cycle) &&
           throws_code(empty, WorkflowDagErrorCode::empty_workflow);
}

[[nodiscard]] bool disconnected_contract() {
    const Workflow workflow{
        Workflow::current_schema_version,
        WorkflowId{"wf-disconnected"},
        "Disconnected roots",
        "",
        {
            node("zeta"),
            node("alpha"),
            node("middle"),
        },
        {},
    };

    const WorkflowDagPlan plan = plan_workflow_dag(workflow);
    return order_values(plan) == std::vector<std::string>{"alpha", "middle", "zeta"} &&
           stage_values(plan) ==
               std::vector<std::vector<std::string>>{{"alpha", "middle", "zeta"}};
}

}  // namespace

int main(const int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: biocore-workflow-dag-tests <case>\n";
        return EXIT_FAILURE;
    }

    const std::string_view test_case{argv[1]};
    bool passed = false;
    if (test_case == "valid") {
        passed = valid_contract();
    } else if (test_case == "deterministic") {
        passed = deterministic_contract();
    } else if (test_case == "missing-node") {
        passed = missing_node_contract();
    } else if (test_case == "missing-port") {
        passed = missing_port_contract();
    } else if (test_case == "duplicate") {
        passed = duplicate_contract();
    } else if (test_case == "self-edge") {
        passed = self_edge_contract();
    } else if (test_case == "cycle") {
        passed = cycle_contract();
    } else if (test_case == "disconnected") {
        passed = disconnected_contract();
    } else {
        std::cerr << "Unknown workflow DAG test case\n";
        return EXIT_FAILURE;
    }

    if (!passed) {
        std::cerr << "Workflow DAG test failed: " << test_case << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "Workflow DAG test passed: " << test_case << '\n';
    return EXIT_SUCCESS;
}
