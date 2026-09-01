#include "biocore/domain/pipeline_definition.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "biocore/domain/component_identity.hpp"

namespace biocore::domain {
namespace {

[[nodiscard]] bool is_blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
               return std::isspace(static_cast<unsigned char>(character)) != 0;
           });
}

void require_text(
    const std::string_view value,
    const std::string_view field_name,
    const std::size_t maximum_length
) {
    if (is_blank(value)) {
        throw std::invalid_argument(std::string{field_name} + " must not be blank");
    }
    if (value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string{field_name} + " must not contain NUL characters");
    }
    if (value.size() > maximum_length) {
        throw std::invalid_argument(std::string{field_name} + " exceeds the maximum length");
    }
}

[[nodiscard]] std::vector<std::size_t> build_topological_order(
    const std::vector<PipelineStep>& steps
) {
    std::unordered_map<std::string_view, std::size_t> indices;
    indices.reserve(steps.size());
    for (std::size_t index = 0U; index < steps.size(); ++index) {
        if (!indices.emplace(steps[index].id(), index).second) {
            throw std::invalid_argument("Pipeline contains duplicate step identifiers");
        }
    }

    std::vector<std::size_t> indegrees(steps.size(), 0U);
    std::vector<std::vector<std::size_t>> dependents(steps.size());
    for (std::size_t index = 0U; index < steps.size(); ++index) {
        for (const std::string& dependency_id : steps[index].depends_on()) {
            const auto dependency = indices.find(dependency_id);
            if (dependency == indices.end()) {
                throw std::invalid_argument("Pipeline step references a missing dependency");
            }
            ++indegrees[index];
            dependents[dependency->second].push_back(index);
        }
    }

    std::priority_queue<
        std::size_t,
        std::vector<std::size_t>,
        std::greater<>
    > ready;
    for (std::size_t index = 0U; index < indegrees.size(); ++index) {
        if (indegrees[index] == 0U) ready.push(index);
    }

    std::vector<std::size_t> order;
    order.reserve(steps.size());
    while (!ready.empty()) {
        const std::size_t current = ready.top();
        ready.pop();
        order.push_back(current);
        for (const std::size_t dependent : dependents[current]) {
            if (--indegrees[dependent] == 0U) ready.push(dependent);
        }
    }
    if (order.size() != steps.size()) {
        throw std::invalid_argument("Pipeline dependency graph contains a cycle");
    }
    return order;
}

}  // namespace

PipelineDefinition::PipelineDefinition(
    const std::uint32_t schema_version,
    std::string id,
    std::string name,
    std::string version,
    std::vector<PipelineStep> steps
)
    : schema_version_{schema_version},
      id_{std::move(id)},
      name_{std::move(name)},
      version_{std::move(version)},
      steps_{std::move(steps)} {
    if (schema_version_ != current_schema_version) {
        throw std::invalid_argument("Pipeline schema version is unsupported");
    }
    require_text(id_, "Pipeline id", maximum_id_length);
    if (!is_namespaced_identifier(id_, maximum_id_length)) {
        throw std::invalid_argument("Pipeline id is invalid");
    }
    require_text(name_, "Pipeline name", maximum_name_length);
    require_text(version_, "Pipeline version", maximum_version_length);
    if (!is_semantic_version(version_, maximum_version_length)) {
        throw std::invalid_argument("Pipeline version must use semantic versioning");
    }
    if (steps_.empty()) {
        throw std::invalid_argument("Pipeline must contain at least one step");
    }
    if (steps_.size() > maximum_steps) {
        throw std::invalid_argument("Pipeline contains too many steps");
    }

    topological_order_ = build_topological_order(steps_);
    for (const PipelineStep& step : steps_) {
        total_weight_ += step.weight();
        if (!std::isfinite(total_weight_)) {
            throw std::invalid_argument("Pipeline total weight is outside the supported range");
        }
    }
    if (total_weight_ <= 0.0) {
        throw std::invalid_argument("Pipeline total weight must be greater than zero");
    }
}

std::uint32_t PipelineDefinition::schema_version() const noexcept { return schema_version_; }
std::string_view PipelineDefinition::id() const noexcept { return id_; }
std::string_view PipelineDefinition::name() const noexcept { return name_; }
std::string_view PipelineDefinition::version() const noexcept { return version_; }
const std::vector<PipelineStep>& PipelineDefinition::steps() const noexcept { return steps_; }
const std::vector<std::size_t>& PipelineDefinition::topological_order() const noexcept {
    return topological_order_;
}
double PipelineDefinition::total_weight() const noexcept { return total_weight_; }

}  // namespace biocore::domain
