#include "biocore/domain/pipeline_step.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

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

}  // namespace

PipelineStep::PipelineStep(
    std::string id,
    std::string module_id,
    std::vector<std::string> depends_on,
    const double weight
)
    : id_{std::move(id)},
      module_id_{std::move(module_id)},
      depends_on_{std::move(depends_on)},
      weight_{weight} {
    require_text(id_, "Pipeline step id", maximum_id_length);
    require_text(module_id_, "Pipeline step module id", maximum_module_id_length);
    if (!std::isfinite(weight_) || weight_ <= 0.0) {
        throw std::invalid_argument("Pipeline step weight must be finite and greater than zero");
    }
    if (depends_on_.size() > maximum_dependencies) {
        throw std::invalid_argument("Pipeline step has too many dependencies");
    }

    std::unordered_set<std::string> unique_dependencies;
    unique_dependencies.reserve(depends_on_.size());
    for (const std::string& dependency : depends_on_) {
        require_text(dependency, "Pipeline dependency id", maximum_id_length);
        if (dependency == id_) {
            throw std::invalid_argument("Pipeline step must not depend on itself");
        }
        if (!unique_dependencies.insert(dependency).second) {
            throw std::invalid_argument("Pipeline step contains a duplicate dependency");
        }
    }
}

std::string_view PipelineStep::id() const noexcept { return id_; }
std::string_view PipelineStep::module_id() const noexcept { return module_id_; }
const std::vector<std::string>& PipelineStep::depends_on() const noexcept { return depends_on_; }
double PipelineStep::weight() const noexcept { return weight_; }

}  // namespace biocore::domain
