#include "biocore/domain/workflow_template.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

#include "biocore/domain/component_identity.hpp"
#include "biocore/domain/workflow_dag.hpp"

namespace biocore::domain {
namespace {

[[nodiscard]] bool is_blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(
        value,
        [](const char character) {
            return std::isspace(static_cast<unsigned char>(character)) != 0;
        }
    );
}

void require_text(
    const std::string_view value,
    const std::string_view field,
    const std::size_t maximum_length
) {
    if (is_blank(value) || value.size() > maximum_length ||
        value.find('\0') != std::string_view::npos ||
        std::ranges::any_of(value, [](const char character) {
            return std::iscntrl(static_cast<unsigned char>(character)) != 0;
        })) {
        throw std::invalid_argument(std::string{field} + " is invalid");
    }
}

void require_optional_text(
    const std::string_view value,
    const std::string_view field,
    const std::size_t maximum_length
) {
    if (value.size() > maximum_length ||
        value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string{field} + " is invalid");
    }
}

}  // namespace

WorkflowTemplate::WorkflowTemplate(
    const std::uint32_t schema_version,
    std::string id,
    std::string version,
    std::string name,
    std::string description,
    Workflow blueprint
)
    : schema_version_{schema_version},
      id_{std::move(id)},
      version_{std::move(version)},
      name_{std::move(name)},
      description_{std::move(description)},
      blueprint_{std::move(blueprint)} {
    if (schema_version_ != current_schema_version) {
        throw std::invalid_argument("Workflow template schema version is unsupported");
    }
    require_text(id_, "Workflow template id", maximum_id_length);
    if (!is_namespaced_identifier(id_, maximum_id_length)) {
        throw std::invalid_argument("Workflow template id is invalid");
    }
    require_text(version_, "Workflow template version", maximum_version_length);
    if (!is_semantic_version(version_, maximum_version_length)) {
        throw std::invalid_argument(
            "Workflow template version must use semantic versioning"
        );
    }
    require_text(name_, "Workflow template name", maximum_name_length);
    require_optional_text(
        description_,
        "Workflow template description",
        maximum_description_length
    );
    if (blueprint_.id().value() != id_) {
        throw std::invalid_argument(
            "Workflow template blueprint id must equal the template id"
        );
    }

    // Reusable templates must be structurally executable when discovered.
    static_cast<void>(plan_workflow_dag(blueprint_));
}

std::uint32_t WorkflowTemplate::schema_version() const noexcept {
    return schema_version_;
}

std::string_view WorkflowTemplate::id() const noexcept {
    return id_;
}

std::string_view WorkflowTemplate::version() const noexcept {
    return version_;
}

std::string_view WorkflowTemplate::name() const noexcept {
    return name_;
}

std::string_view WorkflowTemplate::description() const noexcept {
    return description_;
}

const Workflow& WorkflowTemplate::blueprint() const noexcept {
    return blueprint_;
}

}  // namespace biocore::domain
