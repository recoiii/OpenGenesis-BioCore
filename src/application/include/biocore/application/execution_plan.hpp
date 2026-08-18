#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <optional>
#include <vector>

#include "biocore/domain/plugin_module_definition.hpp"

namespace biocore::application {

enum class ExecutionInputSourceKind {
    managed_file,
    step_output,
};

[[nodiscard]] std::string_view to_string(ExecutionInputSourceKind kind) noexcept;

struct ExecutionParameterBinding final {
    std::string name;
    domain::PluginParameterType type{domain::PluginParameterType::string};
    std::string value;
};

struct ExecutionInputBinding final {
    std::string port_name;
    ExecutionInputSourceKind source_kind{ExecutionInputSourceKind::managed_file};
    std::string source_id;
    std::string file_type;
    std::string relative_project_path;
};

struct ExecutionOutputBinding final {
    std::string port_name;
    std::string file_type;
    std::string relative_project_path;
};

struct ExecutionPlanStep final {
    std::string id;
    std::string module_id;
    std::string plugin_id;
    std::string plugin_version;
    domain::PluginModuleType module_type{domain::PluginModuleType::process};
    std::string plugin_root_path;
    std::string executable_path;
    std::vector<std::string> depends_on;
    double normalized_weight{0.0};
    std::vector<domain::PluginParameterDefinition> parameter_definitions;
    std::vector<domain::PluginInputPortDefinition> input_definitions;
    std::vector<domain::PluginOutputPortDefinition> output_definitions;
    std::vector<ExecutionParameterBinding> parameters;
    std::vector<ExecutionInputBinding> inputs;
    std::vector<ExecutionOutputBinding> outputs;
};

class ExecutionPlan final {
public:
    static constexpr std::uint32_t current_schema_version = 3U;

    ExecutionPlan(
        std::uint32_t schema_version,
        std::string job_id,
        std::int64_t job_revision,
        std::string pipeline_id,
        std::string pipeline_version,
        std::vector<ExecutionPlanStep> steps
    );

    [[nodiscard]] std::uint32_t schema_version() const noexcept;
    [[nodiscard]] std::string_view job_id() const noexcept;
    [[nodiscard]] std::int64_t job_revision() const noexcept;
    [[nodiscard]] std::string_view pipeline_id() const noexcept;
    [[nodiscard]] std::string_view pipeline_version() const noexcept;
    [[nodiscard]] const std::vector<ExecutionPlanStep>& steps() const noexcept;

    [[nodiscard]] double calculate_overall_progress(
        const std::unordered_map<std::string, double>& step_progress
    ) const;

private:
    std::uint32_t schema_version_;
    std::string job_id_;
    std::int64_t job_revision_;
    std::string pipeline_id_;
    std::string pipeline_version_;
    std::vector<ExecutionPlanStep> steps_;
};

}  // namespace biocore::application
