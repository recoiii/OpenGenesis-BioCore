#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace biocore::pipeline_protocol {

inline constexpr std::uint32_t current_pipeline_definition_schema_version = 1U;
inline constexpr std::uint32_t current_execution_plan_schema_version = 3U;
inline constexpr std::size_t maximum_pipeline_document_bytes = 1024U * 1024U;

struct PipelineStepDocument final {
    std::string id;
    std::string module_id;
    std::vector<std::string> depends_on;
    double weight{0.0};
};

struct ExecutionParameterDocument final {
    std::string name;
    std::string type;
    std::string value;
};

struct ExecutionInputBindingDocument final {
    std::string port;
    std::string source_kind;
    std::string source_id;
    std::string file_type;
    std::string relative_project_path;
};

struct ExecutionOutputBindingDocument final {
    std::string port;
    std::string file_type;
    std::string relative_project_path;
};

struct ExecutionPlanStepDocument final {
    std::string id;
    std::string module_id;
    std::string plugin_id;
    std::string plugin_version;
    std::string module_type;
    std::string plugin_root_path;
    std::string executable_path;
    std::vector<std::string> depends_on;
    double weight{0.0};
    std::vector<ExecutionParameterDocument> parameters{};
    std::vector<ExecutionInputBindingDocument> inputs{};
    std::vector<ExecutionOutputBindingDocument> outputs{};
};

struct PipelineDefinitionDocument final {
    std::uint32_t schema_version{current_pipeline_definition_schema_version};
    std::string id;
    std::string name;
    std::string version;
    std::vector<PipelineStepDocument> steps;
};

struct ExecutionPlanDocument final {
    std::uint32_t schema_version{current_execution_plan_schema_version};
    std::string job_id;
    std::int64_t job_revision{0};
    std::string pipeline_id;
    std::string pipeline_version;
    std::vector<ExecutionPlanStepDocument> steps;
};

}  // namespace biocore::pipeline_protocol
