#include "biocore/domain/workflow_dag.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <utility>

namespace biocore::domain {
namespace {

using NodeLookup = std::map<std::string, const WorkflowNode*, std::less<>>;
using DependencyMap = std::map<std::string, std::set<std::string, std::less<>>, std::less<>>;
using DependentMap = std::map<std::string, std::set<std::string, std::less<>>, std::less<>>;

[[nodiscard]] bool has_input_port(
    const WorkflowNode& node,
    const std::string_view port_name
) noexcept {
    return std::ranges::any_of(node.inputs(), [port_name](const WorkflowInputDeclaration& input) {
        return input.name() == port_name;
    });
}

[[nodiscard]] bool has_output_port(
    const WorkflowNode& node,
    const std::string_view port_name
) noexcept {
    return std::ranges::any_of(node.outputs(), [port_name](const WorkflowOutputDeclaration& output) {
        return output.name() == port_name;
    });
}

[[nodiscard]] std::string edge_text(const WorkflowEdge& edge) {
    std::ostringstream output;
    output << edge.source_node_id().value() << '.' << edge.source_output()
           << " -> " << edge.target_node_id().value() << '.' << edge.target_input();
    return output.str();
}

[[noreturn]] void fail(
    const WorkflowDagErrorCode code,
    const std::string& message
) {
    throw WorkflowDagError{code, message};
}

}  // namespace

std::string_view to_string(const WorkflowDagErrorCode code) noexcept {
    switch (code) {
        case WorkflowDagErrorCode::empty_workflow: return "empty_workflow";
        case WorkflowDagErrorCode::missing_source_node: return "missing_source_node";
        case WorkflowDagErrorCode::missing_target_node: return "missing_target_node";
        case WorkflowDagErrorCode::missing_source_output: return "missing_source_output";
        case WorkflowDagErrorCode::missing_target_input: return "missing_target_input";
        case WorkflowDagErrorCode::self_edge: return "self_edge";
        case WorkflowDagErrorCode::duplicate_edge: return "duplicate_edge";
        case WorkflowDagErrorCode::multiple_incoming_connections:
            return "multiple_incoming_connections";
        case WorkflowDagErrorCode::cycle: return "cycle";
    }
    return "unknown";
}

WorkflowDagError::WorkflowDagError(
    const WorkflowDagErrorCode code,
    std::string message
)
    : std::invalid_argument{std::move(message)}, code_{code} {}

WorkflowDagErrorCode WorkflowDagError::code() const noexcept { return code_; }

WorkflowDagPlan::WorkflowDagPlan(
    std::vector<WorkflowPlanNode> ordered_nodes,
    std::vector<std::vector<WorkflowNodeId>> execution_stages
)
    : ordered_nodes_{std::move(ordered_nodes)},
      execution_stages_{std::move(execution_stages)} {}

const std::vector<WorkflowPlanNode>& WorkflowDagPlan::ordered_nodes() const noexcept {
    return ordered_nodes_;
}

const std::vector<std::vector<WorkflowNodeId>>&
WorkflowDagPlan::execution_stages() const noexcept {
    return execution_stages_;
}

WorkflowDagPlan plan_workflow_dag(const Workflow& workflow) {
    if (workflow.nodes().empty()) {
        fail(
            WorkflowDagErrorCode::empty_workflow,
            "Workflow DAG validation failed: workflow contains no executable nodes"
        );
    }

    NodeLookup nodes;
    for (const WorkflowNode& node : workflow.nodes()) {
        nodes.emplace(std::string{node.id().value()}, &node);
    }

    DependencyMap dependencies;
    DependentMap dependents;
    for (const auto& [id, node] : nodes) {
        static_cast<void>(node);
        dependencies.emplace(id, std::set<std::string, std::less<>>{});
        dependents.emplace(id, std::set<std::string, std::less<>>{});
    }

    using EdgeKey = std::tuple<std::string, std::string, std::string, std::string>;
    using TargetPortKey = std::pair<std::string, std::string>;
    std::set<EdgeKey> unique_edges;
    std::set<TargetPortKey> connected_target_ports;

    for (const WorkflowEdge& edge : workflow.edges()) {
        const std::string source_id{edge.source_node_id().value()};
        const std::string target_id{edge.target_node_id().value()};
        const std::string source_output{edge.source_output()};
        const std::string target_input{edge.target_input()};

        if (source_id == target_id) {
            fail(
                WorkflowDagErrorCode::self_edge,
                "Workflow DAG validation failed: self-edge is not allowed: " + edge_text(edge)
            );
        }

        const auto source = nodes.find(source_id);
        if (source == nodes.end()) {
            fail(
                WorkflowDagErrorCode::missing_source_node,
                "Workflow DAG validation failed: edge references missing source node '" +
                    source_id + "'"
            );
        }
        const auto target = nodes.find(target_id);
        if (target == nodes.end()) {
            fail(
                WorkflowDagErrorCode::missing_target_node,
                "Workflow DAG validation failed: edge references missing target node '" +
                    target_id + "'"
            );
        }

        if (!has_output_port(*source->second, source_output)) {
            fail(
                WorkflowDagErrorCode::missing_source_output,
                "Workflow DAG validation failed: source output port '" + source_output +
                    "' does not exist on node '" + source_id + "'"
            );
        }
        if (!has_input_port(*target->second, target_input)) {
            fail(
                WorkflowDagErrorCode::missing_target_input,
                "Workflow DAG validation failed: target input port '" + target_input +
                    "' does not exist on node '" + target_id + "'"
            );
        }

        const EdgeKey edge_key{source_id, source_output, target_id, target_input};
        if (!unique_edges.insert(edge_key).second) {
            fail(
                WorkflowDagErrorCode::duplicate_edge,
                "Workflow DAG validation failed: duplicate edge: " + edge_text(edge)
            );
        }

        const TargetPortKey target_port{target_id, target_input};
        if (!connected_target_ports.insert(target_port).second) {
            fail(
                WorkflowDagErrorCode::multiple_incoming_connections,
                "Workflow DAG validation failed: multiple producers target input '" +
                    target_id + "." + target_input + "'"
            );
        }

        dependencies.at(target_id).insert(source_id);
        dependents.at(source_id).insert(target_id);
    }

    std::map<std::string, std::size_t, std::less<>> indegrees;
    std::set<std::string, std::less<>> ready;
    for (const auto& [id, node_dependencies] : dependencies) {
        indegrees.emplace(id, node_dependencies.size());
        if (node_dependencies.empty()) ready.insert(id);
    }

    std::vector<WorkflowPlanNode> ordered_nodes;
    ordered_nodes.reserve(nodes.size());
    std::vector<std::vector<WorkflowNodeId>> execution_stages;

    while (!ready.empty()) {
        std::vector<std::string> current_stage{ready.begin(), ready.end()};
        ready.clear();

        std::vector<WorkflowNodeId> stage_ids;
        stage_ids.reserve(current_stage.size());

        for (const std::string& id : current_stage) {
            stage_ids.emplace_back(id);

            std::vector<WorkflowNodeId> node_dependencies;
            node_dependencies.reserve(dependencies.at(id).size());
            for (const std::string& dependency : dependencies.at(id)) {
                node_dependencies.emplace_back(dependency);
            }
            ordered_nodes.push_back(
                WorkflowPlanNode{WorkflowNodeId{id}, std::move(node_dependencies)}
            );
        }

        for (const std::string& id : current_stage) {
            for (const std::string& dependent : dependents.at(id)) {
                auto& indegree = indegrees.at(dependent);
                if (indegree == 0U) {
                    throw std::logic_error("Workflow DAG planner encountered an invalid indegree");
                }
                --indegree;
                if (indegree == 0U) ready.insert(dependent);
            }
        }

        execution_stages.push_back(std::move(stage_ids));
    }

    if (ordered_nodes.size() != nodes.size()) {
        fail(
            WorkflowDagErrorCode::cycle,
            "Workflow DAG validation failed: dependency graph contains a cycle"
        );
    }

    return WorkflowDagPlan{std::move(ordered_nodes), std::move(execution_stages)};
}

}  // namespace biocore::domain
