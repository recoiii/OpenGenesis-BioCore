#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace biocore::domain {

class WorkflowId final {
public:
    static constexpr std::size_t maximum_length = 256U;

    explicit WorkflowId(std::string value);

    [[nodiscard]] std::string_view value() const noexcept;

    friend bool operator==(const WorkflowId&, const WorkflowId&) = default;
    friend auto operator<=>(const WorkflowId&, const WorkflowId&) = default;

private:
    std::string value_;
};

class WorkflowNodeId final {
public:
    static constexpr std::size_t maximum_length = 128U;

    explicit WorkflowNodeId(std::string value);

    [[nodiscard]] std::string_view value() const noexcept;

    friend bool operator==(const WorkflowNodeId&, const WorkflowNodeId&) = default;
    friend auto operator<=>(const WorkflowNodeId&, const WorkflowNodeId&) = default;

private:
    std::string value_;
};

class WorkflowInputDeclaration final {
public:
    static constexpr std::size_t maximum_name_length = 64U;
    static constexpr std::size_t maximum_artifact_type_length = 128U;

    WorkflowInputDeclaration(std::string name, std::string artifact_type, bool required);

    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] std::string_view artifact_type() const noexcept;
    [[nodiscard]] bool required() const noexcept;

private:
    std::string name_;
    std::string artifact_type_;
    bool required_;
};

class WorkflowOutputDeclaration final {
public:
    static constexpr std::size_t maximum_name_length = 64U;
    static constexpr std::size_t maximum_artifact_type_length = 128U;

    WorkflowOutputDeclaration(std::string name, std::string artifact_type);

    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] std::string_view artifact_type() const noexcept;

private:
    std::string name_;
    std::string artifact_type_;
};

using WorkflowParameters = std::map<std::string, std::string, std::less<>>;

class WorkflowNode final {
public:
    static constexpr std::size_t maximum_label_length = 256U;
    static constexpr std::size_t maximum_module_id_length = 256U;
    static constexpr std::size_t maximum_plugin_version_length = 64U;
    static constexpr std::size_t maximum_ports = 64U;
    static constexpr std::size_t maximum_parameters = 128U;
    static constexpr std::size_t maximum_parameter_name_length = 64U;
    static constexpr std::size_t maximum_parameter_value_length = 4096U;

    WorkflowNode(
        WorkflowNodeId id,
        std::string label,
        std::string module_id,
        std::string plugin_version,
        std::vector<WorkflowInputDeclaration> inputs = {},
        std::vector<WorkflowOutputDeclaration> outputs = {},
        WorkflowParameters parameters = {}
    );

    [[nodiscard]] const WorkflowNodeId& id() const noexcept;
    [[nodiscard]] std::string_view label() const noexcept;
    [[nodiscard]] std::string_view module_id() const noexcept;
    [[nodiscard]] std::string_view plugin_version() const noexcept;
    [[nodiscard]] const std::vector<WorkflowInputDeclaration>& inputs() const noexcept;
    [[nodiscard]] const std::vector<WorkflowOutputDeclaration>& outputs() const noexcept;
    [[nodiscard]] const WorkflowParameters& parameters() const noexcept;

private:
    WorkflowNodeId id_;
    std::string label_;
    std::string module_id_;
    std::string plugin_version_;
    std::vector<WorkflowInputDeclaration> inputs_;
    std::vector<WorkflowOutputDeclaration> outputs_;
    WorkflowParameters parameters_;
};

class WorkflowEdge final {
public:
    WorkflowEdge(
        WorkflowNodeId source_node_id,
        std::string source_output,
        WorkflowNodeId target_node_id,
        std::string target_input
    );

    [[nodiscard]] const WorkflowNodeId& source_node_id() const noexcept;
    [[nodiscard]] std::string_view source_output() const noexcept;
    [[nodiscard]] const WorkflowNodeId& target_node_id() const noexcept;
    [[nodiscard]] std::string_view target_input() const noexcept;

private:
    WorkflowNodeId source_node_id_;
    std::string source_output_;
    WorkflowNodeId target_node_id_;
    std::string target_input_;
};

class Workflow final {
public:
    static constexpr std::uint32_t current_schema_version = 1U;
    static constexpr std::size_t maximum_name_length = 256U;
    static constexpr std::size_t maximum_description_length = 8192U;
    static constexpr std::size_t maximum_nodes = 512U;
    static constexpr std::size_t maximum_edges = 4096U;

    Workflow(
        std::uint32_t schema_version,
        WorkflowId id,
        std::string name,
        std::string description,
        std::vector<WorkflowNode> nodes,
        std::vector<WorkflowEdge> edges
    );

    [[nodiscard]] std::uint32_t schema_version() const noexcept;
    [[nodiscard]] const WorkflowId& id() const noexcept;
    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] std::string_view description() const noexcept;
    [[nodiscard]] const std::vector<WorkflowNode>& nodes() const noexcept;
    [[nodiscard]] const std::vector<WorkflowEdge>& edges() const noexcept;

private:
    std::uint32_t schema_version_;
    WorkflowId id_;
    std::string name_;
    std::string description_;
    std::vector<WorkflowNode> nodes_;
    std::vector<WorkflowEdge> edges_;
};

}  // namespace biocore::domain
