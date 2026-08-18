#include "biocore/application/execution_plan.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace biocore::application {
namespace {

constexpr double normalized_weight_tolerance = 1.0e-12;
constexpr std::size_t maximum_text_length = 256U;
constexpr std::size_t maximum_path_length = 32U * 1024U;
constexpr std::size_t maximum_steps = 256U;

[[nodiscard]] bool is_blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
               return std::isspace(static_cast<unsigned char>(character)) != 0;
           });
}

void require_text(
    const std::string_view value,
    const std::string_view field,
    const std::size_t maximum_length = maximum_text_length
) {
    if (is_blank(value) || value.find('\0') != std::string_view::npos ||
        value.size() > maximum_length) {
        throw std::invalid_argument(std::string{field} + " is invalid");
    }
}

}  // namespace

std::string_view to_string(const ExecutionInputSourceKind kind) noexcept {
    switch (kind) {
        case ExecutionInputSourceKind::managed_file: return "managedFile";
        case ExecutionInputSourceKind::step_output: return "stepOutput";
    }
    return "unknown";
}

ExecutionPlan::ExecutionPlan(
    const std::uint32_t schema_version,
    std::string job_id,
    const std::int64_t job_revision,
    std::string pipeline_id,
    std::string pipeline_version,
    std::vector<ExecutionPlanStep> steps
)
    : schema_version_{schema_version},
      job_id_{std::move(job_id)},
      job_revision_{job_revision},
      pipeline_id_{std::move(pipeline_id)},
      pipeline_version_{std::move(pipeline_version)},
      steps_{std::move(steps)} {
    if (schema_version_ != current_schema_version) {
        throw std::invalid_argument("Execution plan schema version is unsupported");
    }
    require_text(job_id_, "Execution plan job id");
    require_text(pipeline_id_, "Execution plan pipeline id");
    require_text(pipeline_version_, "Execution plan pipeline version");
    if (job_revision_ < 0) {
        throw std::invalid_argument("Execution plan job revision must not be negative");
    }
    if (steps_.empty() || steps_.size() > maximum_steps) {
        throw std::invalid_argument("Execution plan step count is invalid");
    }

    std::unordered_map<std::string_view, std::size_t> positions;
    positions.reserve(steps_.size());
    double total_weight = 0.0;
    for (std::size_t index = 0U; index < steps_.size(); ++index) {
        const ExecutionPlanStep& step = steps_[index];
        require_text(step.id, "Execution plan step id");
        require_text(step.module_id, "Execution plan module id");
        require_text(step.plugin_id, "Execution plan plugin id");
        require_text(step.plugin_version, "Execution plan plugin version");
        require_text(
            step.plugin_root_path, "Execution plan plugin root path", maximum_path_length
        );
        require_text(
            step.executable_path, "Execution plan executable path", maximum_path_length
        );
        if (!step.module_id.starts_with(step.plugin_id + '.')) {
            throw std::invalid_argument("Execution plan module is outside the plugin namespace");
        }
        if (!positions.emplace(step.id, index).second) {
            throw std::invalid_argument("Execution plan contains duplicate step identifiers");
        }
        if (!std::isfinite(step.normalized_weight) || step.normalized_weight <= 0.0 ||
            step.normalized_weight > 1.0) {
            throw std::invalid_argument("Execution plan step weight is invalid");
        }
        std::unordered_set<std::string_view> binding_names;
        for (const auto& parameter : step.parameters) {
            require_text(parameter.name, "Execution plan parameter name");
            require_text(parameter.value, "Execution plan parameter value", 4096U);
            if (!binding_names.insert(parameter.name).second) {
                throw std::invalid_argument("Execution plan contains duplicate parameter bindings");
            }
        }
        for (const auto& input : step.inputs) {
            require_text(input.port_name, "Execution plan input port");
            require_text(input.source_id, "Execution plan input source id");
            require_text(input.file_type, "Execution plan input file type");
            require_text(input.relative_project_path, "Execution plan input relative path", maximum_path_length);
            if (!binding_names.insert(input.port_name).second) {
                throw std::invalid_argument("Execution plan contains duplicate binding names");
            }
        }
        for (const auto& output : step.outputs) {
            require_text(output.port_name, "Execution plan output port");
            require_text(output.file_type, "Execution plan output file type");
            require_text(output.relative_project_path, "Execution plan output relative path", maximum_path_length);
            if (!binding_names.insert(output.port_name).second) {
                throw std::invalid_argument("Execution plan contains duplicate binding names");
            }
        }
        total_weight += step.normalized_weight;
    }
    if (!std::isfinite(total_weight) ||
        std::abs(total_weight - 1.0) > normalized_weight_tolerance) {
        throw std::invalid_argument("Execution plan normalized weights must sum to one");
    }

    for (std::size_t index = 0U; index < steps_.size(); ++index) {
        std::unordered_set<std::string_view> unique_dependencies;
        for (const std::string& dependency : steps_[index].depends_on) {
            require_text(dependency, "Execution plan dependency id");
            const auto position = positions.find(dependency);
            if (position == positions.end() || position->second >= index) {
                throw std::invalid_argument(
                    "Execution plan dependencies must reference an earlier planned step"
                );
            }
            if (!unique_dependencies.insert(dependency).second) {
                throw std::invalid_argument("Execution plan contains duplicate dependencies");
            }
        }
    }
}

std::uint32_t ExecutionPlan::schema_version() const noexcept { return schema_version_; }
std::string_view ExecutionPlan::job_id() const noexcept { return job_id_; }
std::int64_t ExecutionPlan::job_revision() const noexcept { return job_revision_; }
std::string_view ExecutionPlan::pipeline_id() const noexcept { return pipeline_id_; }
std::string_view ExecutionPlan::pipeline_version() const noexcept { return pipeline_version_; }
const std::vector<ExecutionPlanStep>& ExecutionPlan::steps() const noexcept { return steps_; }

double ExecutionPlan::calculate_overall_progress(
    const std::unordered_map<std::string, double>& step_progress
) const {
    std::unordered_map<std::string_view, double> validated;
    validated.reserve(steps_.size());
    for (const ExecutionPlanStep& step : steps_) {
        validated.emplace(step.id, 0.0);
    }
    for (const auto& [step_id, progress] : step_progress) {
        const auto iterator = validated.find(step_id);
        if (iterator == validated.end()) {
            throw std::invalid_argument("Progress references an unknown execution-plan step");
        }
        if (!std::isfinite(progress) || progress < 0.0 || progress > 1.0) {
            throw std::invalid_argument("Step progress must be finite and between zero and one");
        }
        iterator->second = progress;
    }

    double overall = 0.0;
    for (const ExecutionPlanStep& step : steps_) {
        const double progress = validated.at(step.id);
        if (progress > 0.0) {
            for (const std::string& dependency : step.depends_on) {
                if (validated.at(dependency) != 1.0) {
                    throw std::invalid_argument(
                        "A step cannot report progress before its dependencies complete"
                    );
                }
            }
        }
        overall += step.normalized_weight * progress;
    }
    return std::clamp(overall, 0.0, 1.0);
}

}  // namespace biocore::application
