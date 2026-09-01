#!/usr/bin/env python3
from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, content: str) -> None:
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(content, encoding="utf-8", newline="\n")


def replace_once(path: str, old: str, new: str) -> None:
    content = read(path)
    count = content.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one replacement, found {count}: {old[:80]!r}")
    write(path, content.replace(old, new, 1))


# Shared identity validation keeps plugin and pipeline component rules aligned.
write("src/domain/include/biocore/domain/component_identity.hpp", r'''#pragma once

#include <cstddef>
#include <string_view>

namespace biocore::domain {

[[nodiscard]] bool is_namespaced_identifier(
    std::string_view value,
    std::size_t maximum_length
) noexcept;

[[nodiscard]] bool is_semantic_version(
    std::string_view value,
    std::size_t maximum_length
) noexcept;

}  // namespace biocore::domain
''')

write("src/domain/source/component_identity.cpp", r'''#include "biocore/domain/component_identity.hpp"

#include <algorithm>

namespace biocore::domain {
namespace {

[[nodiscard]] bool is_identifier_character(const char value) noexcept {
    return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
           value == '-';
}

[[nodiscard]] bool valid_version_identifiers(
    const std::string_view value,
    const bool reject_numeric_leading_zero
) noexcept {
    if (value.empty()) return false;
    std::size_t begin = 0U;
    while (begin <= value.size()) {
        const std::size_t end = value.find('.', begin);
        const std::string_view identifier = value.substr(
            begin, end == std::string_view::npos ? value.size() - begin : end - begin
        );
        if (identifier.empty() ||
            !std::ranges::all_of(identifier, [](const char character) {
                return (character >= 'A' && character <= 'Z') ||
                       (character >= 'a' && character <= 'z') ||
                       (character >= '0' && character <= '9') || character == '-';
            })) {
            return false;
        }
        const bool numeric = std::ranges::all_of(identifier, [](const char character) {
            return character >= '0' && character <= '9';
        });
        if (reject_numeric_leading_zero && numeric && identifier.size() > 1U &&
            identifier.front() == '0') {
            return false;
        }
        if (end == std::string_view::npos) break;
        begin = end + 1U;
    }
    return true;
}

}  // namespace

bool is_namespaced_identifier(
    const std::string_view value,
    const std::size_t maximum_length
) noexcept {
    if (value.empty() || value.size() > maximum_length ||
        value.find('\0') != std::string_view::npos) {
        return false;
    }
    bool segment_has_character = false;
    bool previous_hyphen = false;
    for (const char character : value) {
        if (character == '.') {
            if (!segment_has_character || previous_hyphen) return false;
            segment_has_character = false;
            previous_hyphen = false;
            continue;
        }
        if (!is_identifier_character(character) ||
            (!segment_has_character && character == '-')) {
            return false;
        }
        segment_has_character = true;
        previous_hyphen = character == '-';
    }
    return segment_has_character && !previous_hyphen;
}

bool is_semantic_version(
    const std::string_view value,
    const std::size_t maximum_length
) noexcept {
    if (value.empty() || value.size() > maximum_length ||
        value.find('\0') != std::string_view::npos) {
        return false;
    }

    const std::size_t plus = value.find('+');
    if (plus != std::string_view::npos &&
        value.find('+', plus + 1U) != std::string_view::npos) {
        return false;
    }
    const std::string_view core_and_prerelease = value.substr(0U, plus);
    if (plus != std::string_view::npos &&
        !valid_version_identifiers(value.substr(plus + 1U), false)) {
        return false;
    }

    const std::size_t dash = core_and_prerelease.find('-');
    const std::string_view core = core_and_prerelease.substr(0U, dash);
    if (dash != std::string_view::npos &&
        !valid_version_identifiers(core_and_prerelease.substr(dash + 1U), true)) {
        return false;
    }

    std::size_t begin = 0U;
    int components = 0;
    while (begin <= core.size()) {
        const std::size_t end = core.find('.', begin);
        const std::string_view component = core.substr(
            begin, end == std::string_view::npos ? core.size() - begin : end - begin
        );
        if (component.empty() ||
            !std::ranges::all_of(component, [](const char character) {
                return character >= '0' && character <= '9';
            }) ||
            (component.size() > 1U && component.front() == '0')) {
            return false;
        }
        ++components;
        if (end == std::string_view::npos) break;
        begin = end + 1U;
    }
    return components == 3;
}

}  // namespace biocore::domain
''')

replace_once(
    "src/domain/CMakeLists.txt",
    "    PRIVATE\n        source/job.cpp\n",
    "    PRIVATE\n        source/component_identity.cpp\n        source/job.cpp\n",
)
replace_once(
    "src/domain/CMakeLists.txt",
    "        FILES\n            include/biocore/domain/job.hpp\n",
    "        FILES\n            include/biocore/domain/component_identity.hpp\n            include/biocore/domain/job.hpp\n",
)

write("src/domain/include/biocore/domain/pipeline_step.hpp", r'''#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace biocore::domain {

class PipelineStep final {
public:
    static constexpr std::size_t maximum_id_length = 128U;
    static constexpr std::size_t maximum_module_id_length = 256U;
    static constexpr std::size_t maximum_plugin_version_length = 64U;
    static constexpr std::size_t maximum_dependencies = 64U;

    PipelineStep(
        std::string id,
        std::string module_id,
        std::string plugin_version,
        std::vector<std::string> depends_on,
        double weight
    );

    [[nodiscard]] std::string_view id() const noexcept;
    [[nodiscard]] std::string_view module_id() const noexcept;
    [[nodiscard]] std::string_view plugin_version() const noexcept;
    [[nodiscard]] const std::vector<std::string>& depends_on() const noexcept;
    [[nodiscard]] double weight() const noexcept;

private:
    std::string id_;
    std::string module_id_;
    std::string plugin_version_;
    std::vector<std::string> depends_on_;
    double weight_;
};

}  // namespace biocore::domain
''')

write("src/domain/source/pipeline_step.cpp", r'''#include "biocore/domain/pipeline_step.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
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

}  // namespace

PipelineStep::PipelineStep(
    std::string id,
    std::string module_id,
    std::string plugin_version,
    std::vector<std::string> depends_on,
    const double weight
)
    : id_{std::move(id)},
      module_id_{std::move(module_id)},
      plugin_version_{std::move(plugin_version)},
      depends_on_{std::move(depends_on)},
      weight_{weight} {
    require_text(id_, "Pipeline step id", maximum_id_length);
    require_text(module_id_, "Pipeline step module id", maximum_module_id_length);
    if (!is_namespaced_identifier(module_id_, maximum_module_id_length)) {
        throw std::invalid_argument("Pipeline step module id is invalid");
    }
    if (!is_semantic_version(plugin_version_, maximum_plugin_version_length)) {
        throw std::invalid_argument("Pipeline step plugin version must use semantic versioning");
    }
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
std::string_view PipelineStep::plugin_version() const noexcept { return plugin_version_; }
const std::vector<std::string>& PipelineStep::depends_on() const noexcept { return depends_on_; }
double PipelineStep::weight() const noexcept { return weight_; }

}  // namespace biocore::domain
''')

replace_once(
    "src/domain/include/biocore/domain/pipeline_definition.hpp",
    "static constexpr std::uint32_t current_schema_version = 1U;",
    "static constexpr std::uint32_t current_schema_version = 2U;",
)
replace_once(
    "src/domain/source/pipeline_definition.cpp",
    "#include <utility>\n",
    "#include <utility>\n\n#include \"biocore/domain/component_identity.hpp\"\n",
)
replace_once(
    "src/domain/source/pipeline_definition.cpp",
    "    require_text(id_, \"Pipeline id\", maximum_id_length);\n    require_text(name_, \"Pipeline name\", maximum_name_length);\n    require_text(version_, \"Pipeline version\", maximum_version_length);\n",
    "    require_text(id_, \"Pipeline id\", maximum_id_length);\n    if (!is_namespaced_identifier(id_, maximum_id_length)) {\n        throw std::invalid_argument(\"Pipeline id is invalid\");\n    }\n    require_text(name_, \"Pipeline name\", maximum_name_length);\n    require_text(version_, \"Pipeline version\", maximum_version_length);\n    if (!is_semantic_version(version_, maximum_version_length)) {\n        throw std::invalid_argument(\"Pipeline version must use semantic versioning\");\n    }\n",
)

write("src/pipeline_protocol/include/biocore/pipeline_protocol/pipeline_document.hpp", r'''#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace biocore::pipeline_protocol {

inline constexpr std::uint32_t current_pipeline_definition_schema_version = 2U;
inline constexpr std::uint32_t current_execution_plan_schema_version = 4U;
inline constexpr std::size_t maximum_pipeline_document_bytes = 1024U * 1024U;

struct PipelineStepDocument final {
    std::string id;
    std::string module_id;
    std::string plugin_version;
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
    std::uint32_t plugin_manifest_version{2U};
    std::string plugin_api_version{"1.0"};
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
''')

# Pipeline codec: exact step plugin pin and execution-plan manifest/API metadata.
replace_once(
    "src/pipeline_protocol/source/pipeline_document_codec.cpp",
    'require_only_fields(object, {"id", "module", "dependsOn", "weight"});\n        steps.push_back(PipelineStepDocument{\n            .id = require_string(object, "id"),\n            .module_id = require_string(object, "module"),\n            .depends_on = parse_dependencies(require_field(object, "dependsOn")),',
    'require_only_fields(object, {"id", "module", "pluginVersion", "dependsOn", "weight"});\n        steps.push_back(PipelineStepDocument{\n            .id = require_string(object, "id"),\n            .module_id = require_string(object, "module"),\n            .plugin_version = require_string(object, "pluginVersion"),\n            .depends_on = parse_dependencies(require_field(object, "dependsOn")),',
)
replace_once(
    "src/pipeline_protocol/source/pipeline_document_codec.cpp",
    '[[nodiscard]] double require_number(const JsonObject& object, const std::string_view field) {',
    '[[nodiscard]] std::uint32_t require_uint32(const JsonObject& object, const std::string_view field) {\n    const std::int64_t value = require_integer(object, field);\n    if (value < 0 || value > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {\n        throw std::invalid_argument("Pipeline JSON field is outside uint32 range: " + std::string{field});\n    }\n    return static_cast<std::uint32_t>(value);\n}\n\n[[nodiscard]] double require_number(const JsonObject& object, const std::string_view field) {',
)
replace_once(
    "src/pipeline_protocol/source/pipeline_document_codec.cpp",
    '{"id", "module", "pluginId", "pluginVersion", "moduleType",\n             "pluginRoot", "executable", "dependsOn", "weight", "parameters", "inputs", "outputs"}',
    '{"id", "module", "pluginId", "pluginVersion", "pluginManifestVersion",\n             "pluginApiVersion", "moduleType", "pluginRoot", "executable", "dependsOn",\n             "weight", "parameters", "inputs", "outputs"}',
)
replace_once(
    "src/pipeline_protocol/source/pipeline_document_codec.cpp",
    '            .plugin_version = require_string(object, "pluginVersion"),\n            .module_type = require_string(object, "moduleType"),',
    '            .plugin_version = require_string(object, "pluginVersion"),\n            .plugin_manifest_version = require_uint32(object, "pluginManifestVersion"),\n            .plugin_api_version = require_string(object, "pluginApiVersion"),\n            .module_type = require_string(object, "moduleType"),',
)
replace_once(
    "src/pipeline_protocol/source/pipeline_document_codec.cpp",
    '        output << ",\\\"module\\\":";\n        append_string(output, step.module_id);\n        output << ",\\\"dependsOn\\\":[";',
    '        output << ",\\\"module\\\":";\n        append_string(output, step.module_id);\n        output << ",\\\"pluginVersion\\\":";\n        append_string(output, step.plugin_version);\n        output << ",\\\"dependsOn\\\":[";',
)
replace_once(
    "src/pipeline_protocol/source/pipeline_document_codec.cpp",
    '        output << ",\\\"pluginVersion\\\":"; append_string(output, step.plugin_version);\n        output << ",\\\"moduleType\\\":"; append_string(output, step.module_type);',
    '        output << ",\\\"pluginVersion\\\":"; append_string(output, step.plugin_version);\n        output << ",\\\"pluginManifestVersion\\\":" << step.plugin_manifest_version;\n        output << ",\\\"pluginApiVersion\\\":"; append_string(output, step.plugin_api_version);\n        output << ",\\\"moduleType\\\":"; append_string(output, step.module_type);',
)
replace_once(
    "src/pipeline_protocol/source/pipeline_document_codec.cpp",
    '        require_serializable_text(step.module_id, "Pipeline document module id");\n        if (!std::isfinite(step.weight)',
    '        require_serializable_text(step.module_id, "Pipeline document module id");\n        require_serializable_text(step.plugin_version, "Pipeline document plugin version");\n        if (!std::isfinite(step.weight)',
)
replace_once(
    "src/pipeline_protocol/source/pipeline_document_codec.cpp",
    '        require_serializable_text(step.plugin_version, "Execution-plan plugin version");\n        require_serializable_text(step.module_type, "Execution-plan module type");',
    '        require_serializable_text(step.plugin_version, "Execution-plan plugin version");\n        if (step.plugin_manifest_version == 0U) {\n            throw std::invalid_argument("Execution-plan plugin manifest version is invalid");\n        }\n        require_serializable_text(step.plugin_api_version, "Execution-plan plugin API version");\n        require_serializable_text(step.module_type, "Execution-plan module type");',
)

replace_once(
    "src/infrastructure/source/json_pipeline_definition_loader.cpp",
    '        steps.emplace_back(step.id, step.module_id, step.depends_on, step.weight);',
    '        steps.emplace_back(\n            step.id, step.module_id, step.plugin_version, step.depends_on, step.weight\n        );',
)

write("src/application/include/biocore/application/i_plugin_registry.hpp", r'''#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/domain/plugin_module_definition.hpp"

namespace biocore::application {

struct RegisteredPlugin final {
    std::string id;
    std::string name;
    std::string version;
    std::uint32_t manifest_version{2U};
    std::string api_version{"1.0"};
    std::string publisher;
    std::string root_path;
};

struct ResolvedPluginModule final {
    std::string plugin_id;
    std::string plugin_version;
    std::uint32_t plugin_manifest_version{2U};
    std::string plugin_api_version{"1.0"};
    std::string module_id;
    domain::PluginModuleType module_type{domain::PluginModuleType::process};
    std::string plugin_root_path;
    std::string executable_path;
    std::vector<domain::PluginParameterDefinition> parameters{};
    std::vector<domain::PluginInputPortDefinition> inputs{};
    std::vector<domain::PluginOutputPortDefinition> outputs{};
};

class IPluginRegistry {
public:
    virtual ~IPluginRegistry() = default;

    [[nodiscard]] virtual std::optional<ResolvedPluginModule> find_module(
        std::string_view module_id
    ) const = 0;
    [[nodiscard]] virtual std::vector<RegisteredPlugin> list_plugins() const = 0;
};

}  // namespace biocore::application
''')

replace_once(
    "src/infrastructure/source/filesystem_plugin_registry.cpp",
    '            .version = std::string{manifest.version()},\n            .publisher = std::string{manifest.publisher()},',
    '            .version = std::string{manifest.version()},\n            .manifest_version = manifest.manifest_version(),\n            .api_version = std::string{manifest.api_version()},\n            .publisher = std::string{manifest.publisher()},',
)
replace_once(
    "src/infrastructure/source/filesystem_plugin_registry.cpp",
    '            .plugin_version = std::string{manifest.version()},\n            .module_id = std::string{module.id()},',
    '            .plugin_version = std::string{manifest.version()},\n            .plugin_manifest_version = manifest.manifest_version(),\n            .plugin_api_version = std::string{manifest.api_version()},\n            .module_id = std::string{module.id()},',
)

write("src/application/source/pipeline_planner.cpp", r'''#include "biocore/application/pipeline_planner.hpp"

#include <stdexcept>
#include <string>
#include <vector>

#include "biocore/domain/plugin_manifest.hpp"

namespace biocore::application {

ExecutionPlan PipelinePlanner::create_execution_plan(
    const domain::PipelineDefinition& definition,
    const std::string_view job_id,
    const std::int64_t job_revision,
    const IPluginRegistry& plugin_registry
) {
    std::vector<ExecutionPlanStep> planned_steps;
    planned_steps.reserve(definition.steps().size());
    for (const std::size_t index : definition.topological_order()) {
        const domain::PipelineStep& step = definition.steps().at(index);
        const auto resolved = plugin_registry.find_module(step.module_id());
        if (!resolved.has_value()) {
            throw std::invalid_argument(
                "Pipeline references an unavailable plugin module: " +
                std::string{step.module_id()}
            );
        }
        if (resolved->module_id != step.module_id()) {
            throw std::invalid_argument("Plugin registry returned a mismatched module identity");
        }
        if (resolved->plugin_version != step.plugin_version()) {
            throw std::invalid_argument(
                "Pipeline plugin version pin does not match the discovered module: " +
                std::string{step.module_id()}
            );
        }
        if (resolved->plugin_manifest_version < domain::PluginManifest::minimum_manifest_version ||
            resolved->plugin_manifest_version > domain::PluginManifest::current_manifest_version) {
            throw std::invalid_argument("Resolved plugin manifest version is unsupported");
        }
        if (resolved->plugin_api_version != domain::PluginManifest::supported_api_version) {
            throw std::invalid_argument("Resolved plugin API version is unsupported");
        }
        if (!resolved->module_id.starts_with(resolved->plugin_id + '.')) {
            throw std::invalid_argument("Resolved plugin module is outside its plugin namespace");
        }
        planned_steps.push_back(ExecutionPlanStep{
            .id = std::string{step.id()},
            .module_id = std::string{step.module_id()},
            .plugin_id = resolved->plugin_id,
            .plugin_version = resolved->plugin_version,
            .plugin_manifest_version = resolved->plugin_manifest_version,
            .plugin_api_version = resolved->plugin_api_version,
            .module_type = resolved->module_type,
            .plugin_root_path = resolved->plugin_root_path,
            .executable_path = resolved->executable_path,
            .depends_on = step.depends_on(),
            .normalized_weight = step.weight() / definition.total_weight(),
            .parameter_definitions = resolved->parameters,
            .input_definitions = resolved->inputs,
            .output_definitions = resolved->outputs,
            .parameters = {},
            .inputs = {},
            .outputs = {},
        });
    }

    return ExecutionPlan{
        ExecutionPlan::current_schema_version,
        std::string{job_id},
        job_revision,
        std::string{definition.id()},
        std::string{definition.version()},
        std::move(planned_steps),
    };
}

}  // namespace biocore::application
''')

replace_once(
    "src/application/include/biocore/application/execution_plan.hpp",
    '    std::string plugin_version;\n    domain::PluginModuleType module_type',
    '    std::string plugin_version;\n    std::uint32_t plugin_manifest_version{2U};\n    std::string plugin_api_version{"1.0"};\n    domain::PluginModuleType module_type',
)
replace_once(
    "src/application/include/biocore/application/execution_plan.hpp",
    'static constexpr std::uint32_t current_schema_version = 3U;',
    'static constexpr std::uint32_t current_schema_version = 4U;',
)
replace_once(
    "src/application/source/execution_plan.cpp",
    '#include <utility>\n',
    '#include <utility>\n\n#include "biocore/domain/component_identity.hpp"\n#include "biocore/domain/plugin_manifest.hpp"\n',
)
replace_once(
    "src/application/source/execution_plan.cpp",
    '    require_text(pipeline_id_, "Execution plan pipeline id");\n    require_text(pipeline_version_, "Execution plan pipeline version");',
    '    require_text(pipeline_id_, "Execution plan pipeline id");\n    if (!domain::is_namespaced_identifier(pipeline_id_, 256U)) {\n        throw std::invalid_argument("Execution plan pipeline id is invalid");\n    }\n    require_text(pipeline_version_, "Execution plan pipeline version");\n    if (!domain::is_semantic_version(pipeline_version_, 64U)) {\n        throw std::invalid_argument("Execution plan pipeline version is invalid");\n    }',
)
replace_once(
    "src/application/source/execution_plan.cpp",
    '        require_text(step.module_id, "Execution plan module id");\n        require_text(step.plugin_id, "Execution plan plugin id");\n        require_text(step.plugin_version, "Execution plan plugin version");',
    '        require_text(step.module_id, "Execution plan module id");\n        if (!domain::is_namespaced_identifier(step.module_id, 256U)) {\n            throw std::invalid_argument("Execution plan module id is invalid");\n        }\n        require_text(step.plugin_id, "Execution plan plugin id");\n        if (!domain::is_namespaced_identifier(step.plugin_id, 128U)) {\n            throw std::invalid_argument("Execution plan plugin id is invalid");\n        }\n        require_text(step.plugin_version, "Execution plan plugin version");\n        if (!domain::is_semantic_version(step.plugin_version, 64U)) {\n            throw std::invalid_argument("Execution plan plugin version is invalid");\n        }\n        if (step.plugin_manifest_version < domain::PluginManifest::minimum_manifest_version ||\n            step.plugin_manifest_version > domain::PluginManifest::current_manifest_version) {\n            throw std::invalid_argument("Execution plan plugin manifest version is unsupported");\n        }\n        require_text(step.plugin_api_version, "Execution plan plugin API version", 32U);\n        if (step.plugin_api_version != domain::PluginManifest::supported_api_version) {\n            throw std::invalid_argument("Execution plan plugin API version is unsupported");\n        }',
)

replace_once(
    "src/infrastructure/source/json_execution_plan_store.cpp",
    '            .plugin_version = step.plugin_version,\n            .module_type = std::string{domain::to_string(step.module_type)},',
    '            .plugin_version = step.plugin_version,\n            .plugin_manifest_version = step.plugin_manifest_version,\n            .plugin_api_version = step.plugin_api_version,\n            .module_type = std::string{domain::to_string(step.module_type)},',
)

replace_once(
    "apps/worker/main.cpp",
    '            .plugin_version = step.plugin_version,\n            .module_type = *module_type,',
    '            .plugin_version = step.plugin_version,\n            .plugin_manifest_version = step.plugin_manifest_version,\n            .plugin_api_version = step.plugin_api_version,\n            .module_type = *module_type,',
)

# Upgrade all shipped pipeline definitions from schema v1 to v2 using exact plugin versions.
module_versions: dict[str, str] = {}
for manifest_path in sorted((ROOT / "plugins").glob("*/plugin.json")):
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    version = manifest["version"]
    for module in manifest["modules"]:
        module_versions[module["id"]] = version

pipeline_paths = sorted((ROOT / "pipelines").glob("*.json"))
if len(pipeline_paths) != 12:
    raise RuntimeError(f"Expected exactly 12 shipped pipelines, found {len(pipeline_paths)}")
for pipeline_path in pipeline_paths:
    document = json.loads(pipeline_path.read_text(encoding="utf-8"))
    if document.get("schemaVersion") != 1:
        raise RuntimeError(f"{pipeline_path}: expected schemaVersion 1 before upgrade")
    document["schemaVersion"] = 2
    for step in document["steps"]:
        module_id = step["module"]
        if module_id not in module_versions:
            raise RuntimeError(f"{pipeline_path}: module has no shipped plugin: {module_id}")
        step["pluginVersion"] = module_versions[module_id]
    pipeline_path.write_text(
        json.dumps(document, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
        newline="\n",
    )

write("pipelines/README.md", """# OpenGenesis-BioCore pipelines\n\nBundled pipeline definitions use pipeline schema v2. Every step pins an exact semantic `pluginVersion`; planning fails closed if the discovered module belongs to a different plugin version.\n""")

# Update legacy in-tree C++ PipelineStep fixtures by adding the exact demo pin.
def inject_pipeline_step_versions(content: str) -> str:
    marker = "PipelineStep{"
    index = 0
    output = []
    while True:
        start = content.find(marker, index)
        if start < 0:
            output.append(content[index:])
            break
        output.append(content[index:start])
        brace = start + len("PipelineStep")
        pos = brace + 1
        depth = 1
        string_quote = None
        escaped = False
        commas = []
        while pos < len(content) and depth:
            ch = content[pos]
            if string_quote is not None:
                if escaped:
                    escaped = False
                elif ch == "\\":
                    escaped = True
                elif ch == string_quote:
                    string_quote = None
            else:
                if ch in ('"', "'"):
                    string_quote = ch
                elif ch == '{':
                    depth += 1
                elif ch == '}':
                    depth -= 1
                elif ch == ',' and depth == 1:
                    commas.append(pos)
            pos += 1
        if depth != 0:
            raise RuntimeError("Unterminated PipelineStep initializer")
        chunk = content[start:pos]
        if len(commas) == 3:
            relative = commas[1] - start + 1
            chunk = chunk[:relative] + ' "0.1.0",' + chunk[relative:]
        output.append(chunk)
        index = pos
    return "".join(output)

for test_path in sorted((ROOT / "tests").glob("*.cpp")):
    text = test_path.read_text(encoding="utf-8")
    text = inject_pipeline_step_versions(text)
    text = re.sub(
        r'((?:biocore::domain::)?PipelineDefinition(?:\s+\w+)?\s*\{\s*)1U\s*,',
        r'\g<1>2U,',
        text,
    )
    test_path.write_text(text, encoding="utf-8", newline="\n")

# Focused fixture rewrites for strict schema/document tests.
write("tests/pipeline_definition_tests.cpp", r'''#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "biocore/domain/pipeline_definition.hpp"
#include "biocore/domain/pipeline_step.hpp"

namespace {

using biocore::domain::PipelineDefinition;
using biocore::domain::PipelineStep;

template <typename Function>
[[nodiscard]] bool rejects(Function&& function) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

[[nodiscard]] PipelineDefinition make_valid() {
    return PipelineDefinition{
        2U,
        "org.biocore.demo",
        "Demo pipeline",
        "1.0.0",
        {
            PipelineStep{"validate", "org.biocore.demo.validate", "0.1.0", {}, 2.0},
            PipelineStep{"scan", "org.biocore.demo.scan", "0.1.0", {"validate"}, 6.0},
            PipelineStep{"report", "org.biocore.demo.report", "0.1.0", {"scan"}, 2.0},
        },
    };
}

[[nodiscard]] bool valid_dag_contract() {
    const PipelineDefinition definition = make_valid();
    return definition.schema_version() == 2U && definition.id() == "org.biocore.demo" &&
           definition.steps().size() == 3U &&
           definition.steps()[0].plugin_version() == "0.1.0" &&
           definition.topological_order() == std::vector<std::size_t>{0U, 1U, 2U} &&
           std::abs(definition.total_weight() - 10.0) < 1.0e-12;
}

[[nodiscard]] bool deterministic_parallel_order_contract() {
    const PipelineDefinition definition{
        2U,
        "org.biocore.parallel",
        "Parallel",
        "1.0.0",
        {
            PipelineStep{"root-b", "org.biocore.demo.module-b", "0.1.0", {}, 1.0},
            PipelineStep{"root-a", "org.biocore.demo.module-a", "0.1.0", {}, 1.0},
            PipelineStep{"join", "org.biocore.demo.module-join", "0.1.0", {"root-a", "root-b"}, 1.0},
        },
    };
    return definition.topological_order() == std::vector<std::size_t>{0U, 1U, 2U};
}

[[nodiscard]] bool step_invariant_contract() {
    return rejects([] { static_cast<void>(PipelineStep{"", "org.biocore.demo.module", "0.1.0", {}, 1.0}); }) &&
           rejects([] { static_cast<void>(PipelineStep{"step", "", "0.1.0", {}, 1.0}); }) &&
           rejects([] { static_cast<void>(PipelineStep{"step", "org.biocore.demo.module", "1", {}, 1.0}); }) &&
           rejects([] { static_cast<void>(PipelineStep{"step", "org.biocore.demo.module", "0.1.0", {}, 0.0}); }) &&
           rejects([] { static_cast<void>(PipelineStep{"step", "org.biocore.demo.module", "0.1.0", {"step"}, 1.0}); }) &&
           rejects([] {
               static_cast<void>(PipelineStep{"step", "org.biocore.demo.module", "0.1.0", {"a", "a"}, 1.0});
           });
}

[[nodiscard]] bool graph_rejection_contract() {
    return rejects([] {
               static_cast<void>(PipelineDefinition{
                   3U, "org.biocore.pipeline", "Pipeline", "1.0.0",
                   {PipelineStep{"a", "org.biocore.demo.m", "0.1.0", {}, 1.0}}
               });
           }) &&
           rejects([] {
               static_cast<void>(PipelineDefinition{
                   2U, "Pipeline", "Pipeline", "1.0.0",
                   {PipelineStep{"a", "org.biocore.demo.m", "0.1.0", {}, 1.0}}
               });
           }) &&
           rejects([] {
               static_cast<void>(PipelineDefinition{
                   2U, "org.biocore.pipeline", "Pipeline", "1",
                   {PipelineStep{"a", "org.biocore.demo.m", "0.1.0", {}, 1.0}}
               });
           }) &&
           rejects([] {
               static_cast<void>(PipelineDefinition{2U, "org.biocore.pipeline", "Pipeline", "1.0.0", {}});
           }) &&
           rejects([] {
               static_cast<void>(PipelineDefinition{
                   2U, "org.biocore.pipeline", "Pipeline", "1.0.0",
                   {PipelineStep{"a", "org.biocore.demo.m", "0.1.0", {}, 1.0}, PipelineStep{"a", "org.biocore.demo.m", "0.1.0", {}, 1.0}},
               });
           }) &&
           rejects([] {
               static_cast<void>(PipelineDefinition{
                   2U, "org.biocore.pipeline", "Pipeline", "1.0.0",
                   {PipelineStep{"a", "org.biocore.demo.m", "0.1.0", {"missing"}, 1.0}},
               });
           }) &&
           rejects([] {
               static_cast<void>(PipelineDefinition{
                   2U,
                   "org.biocore.pipeline",
                   "Pipeline",
                   "1.0.0",
                   {
                       PipelineStep{"a", "org.biocore.demo.m", "0.1.0", {"b"}, 1.0},
                       PipelineStep{"b", "org.biocore.demo.m", "0.1.0", {"a"}, 1.0},
                   },
               });
           });
}

}  // namespace

int main() {
    const bool passed = valid_dag_contract() && deterministic_parallel_order_contract() &&
                        step_invariant_contract() && graph_rejection_contract();
    if (!passed) {
        std::cerr << "Pipeline definition tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Pipeline definition tests passed\n";
    return EXIT_SUCCESS;
}
''')

write("tests/pipeline_document_codec_tests.cpp", r'''#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "biocore/pipeline_protocol/pipeline_document.hpp"
#include "biocore/pipeline_protocol/pipeline_document_codec.hpp"

namespace {

using namespace biocore::pipeline_protocol;

template <typename Function>
[[nodiscard]] bool rejects(Function&& function) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

[[nodiscard]] PipelineDefinitionDocument definition_document() {
    return PipelineDefinitionDocument{
        .schema_version = current_pipeline_definition_schema_version,
        .id = "org.biocore.demo",
        .name = "Demo \"pipeline\"",
        .version = "1.0.0",
        .steps = {
            PipelineStepDocument{"validate", "org.biocore.demo.validate", "0.1.0", {}, 0.2},
            PipelineStepDocument{"scan", "org.biocore.demo.scan", "0.1.0", {"validate"}, 0.8},
        },
    };
}

[[nodiscard]] bool round_trip_contract() {
    const auto definition = definition_document();
    const std::string encoded = serialize_pipeline_definition_document(definition);
    const auto decoded = parse_pipeline_definition_document(encoded);
    if (decoded.id != definition.id || decoded.name != definition.name ||
        decoded.steps.size() != 2U || decoded.steps[0].plugin_version != "0.1.0" ||
        decoded.steps[1].depends_on != std::vector<std::string>{"validate"}) {
        return false;
    }

    const ExecutionPlanDocument plan{
        .schema_version = current_execution_plan_schema_version,
        .job_id = "job-1",
        .job_revision = 4,
        .pipeline_id = definition.id,
        .pipeline_version = definition.version,
        .steps = {
            ExecutionPlanStepDocument{
                .id = "validate",
                .module_id = "org.biocore.demo.validate",
                .plugin_id = "org.biocore.demo",
                .plugin_version = "0.1.0",
                .plugin_manifest_version = 2U,
                .plugin_api_version = "1.0",
                .module_type = "process",
                .plugin_root_path = "/plugins/org.biocore.demo",
                .executable_path = "/plugins/org.biocore.demo/bin/demo",
                .depends_on = {},
                .weight = 0.2,
                .parameters = {}, .inputs = {}, .outputs = {},
            },
            ExecutionPlanStepDocument{
                .id = "scan",
                .module_id = "org.biocore.demo.scan",
                .plugin_id = "org.biocore.demo",
                .plugin_version = "0.1.0",
                .plugin_manifest_version = 2U,
                .plugin_api_version = "1.0",
                .module_type = "process",
                .plugin_root_path = "/plugins/org.biocore.demo",
                .executable_path = "/plugins/org.biocore.demo/bin/demo",
                .depends_on = {"validate"},
                .weight = 0.8,
                .parameters = {}, .inputs = {}, .outputs = {},
            },
        },
    };
    const auto decoded_plan = parse_execution_plan_document(
        serialize_execution_plan_document(plan)
    );
    return decoded_plan.job_id == "job-1" && decoded_plan.job_revision == 4 &&
           decoded_plan.steps[0].module_id == "org.biocore.demo.validate" &&
           decoded_plan.steps[0].plugin_manifest_version == 2U &&
           decoded_plan.steps[0].plugin_api_version == "1.0";
}

[[nodiscard]] bool strict_schema_contract() {
    return rejects([] {
               static_cast<void>(parse_pipeline_definition_document(
                   R"({"schemaVersion":1,"id":"org.biocore.p","name":"P","version":"1.0.0","steps":[{"id":"a","module":"org.biocore.demo.m","dependsOn":[],"weight":1}]})"
               ));
           }) &&
           rejects([] {
               static_cast<void>(parse_pipeline_definition_document(
                   R"({"schemaVersion":2,"id":"org.biocore.p","name":"P","version":"1.0.0","steps":[{"id":"a","module":"org.biocore.demo.m","dependsOn":[],"weight":1}]})"
               ));
           }) &&
           rejects([] {
               static_cast<void>(parse_pipeline_definition_document(
                   R"({"schemaVersion":2,"id":"org.biocore.p","id":"org.biocore.q","name":"P","version":"1.0.0","steps":[{"id":"a","module":"org.biocore.demo.m","pluginVersion":"0.1.0","dependsOn":[],"weight":1}]})"
               ));
           }) &&
           rejects([] {
               static_cast<void>(parse_execution_plan_document(
                   R"({"schemaVersion":3,"jobId":"j","jobRevision":0,"pipelineId":"org.biocore.p","pipelineVersion":"1.0.0","steps":[]})"
               ));
           }) &&
           rejects([] {
               std::string invalid = R"({"schemaVersion":2,"id":")";
               invalid.push_back(static_cast<char>(0xFF));
               invalid += R"(","name":"P","version":"1.0.0","steps":[]})";
               static_cast<void>(parse_pipeline_definition_document(invalid));
           }) &&
           rejects([] {
               auto document = definition_document();
               document.name = std::string{"bad\0name", 8U};
               static_cast<void>(serialize_pipeline_definition_document(document));
           });
}

}  // namespace

int main() {
    if (!round_trip_contract() || !strict_schema_contract()) {
        std::cerr << "Pipeline document codec tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Pipeline document codec tests passed\n";
    return EXIT_SUCCESS;
}
''')

# Known JSON-pipeline fixtures.
replace_once(
    "tests/filesystem_pipeline_catalog_tests.cpp",
    'R"({\\"schemaVersion\\":1,\\"id\\":\\"org.biocore.demo.validation\\",\\"name\\":\\"Demo\\",\\"version\\":\\"0.1.0\\",\\"steps\\":[{\\"id\\":\\"validate\\",\\"module\\":\\"org.biocore.demo.validate\\",\\"dependsOn\\":[],\\"weight\\":1.0}]})"',
    'R"({\\"schemaVersion\\":2,\\"id\\":\\"org.biocore.demo.validation\\",\\"name\\":\\"Demo\\",\\"version\\":\\"0.1.0\\",\\"steps\\":[{\\"id\\":\\"validate\\",\\"module\\":\\"org.biocore.demo.validate\\",\\"pluginVersion\\":\\"0.1.0\\",\\"dependsOn\\":[],\\"weight\\":1.0}]})"',
)

# json_pipeline_io has several explicit fixtures; rewrite as one complete, focused file.
json_io = read("tests/json_pipeline_io_tests.cpp")
json_io = json_io.replace(
    r'{\"schemaVersion\":1,\"id\":\"org.biocore.demo\",\"name\":\"Demo\",\"version\":\"1.0.0\",\"steps\":[{\"id\":\"validate\",\"module\":\"org.biocore.demo.validate\",\"dependsOn\":[],\"weight\":2},{\"id\":\"scan\",\"module\":\"org.biocore.demo.scan\",\"dependsOn\":[\"validate\"],\"weight\":6},{\"id\":\"report\",\"module\":\"org.biocore.demo.report\",\"dependsOn\":[\"scan\"],\"weight\":2}]}',
    r'{\"schemaVersion\":2,\"id\":\"org.biocore.demo\",\"name\":\"Demo\",\"version\":\"1.0.0\",\"steps\":[{\"id\":\"validate\",\"module\":\"org.biocore.demo.validate\",\"pluginVersion\":\"0.1.0\",\"dependsOn\":[],\"weight\":2},{\"id\":\"scan\",\"module\":\"org.biocore.demo.scan\",\"pluginVersion\":\"0.1.0\",\"dependsOn\":[\"validate\"],\"weight\":6},{\"id\":\"report\",\"module\":\"org.biocore.demo.report\",\"pluginVersion\":\"0.1.0\",\"dependsOn\":[\"scan\"],\"weight\":2}]}',
)
json_io = json_io.replace(
    r'{\"schemaVersion\":1,\"id\":\"p\",\"name\":\"P\",\"version\":\"1\",\"steps\":[{\"id\":\"a\",\"module\":\"m\",\"dependsOn\":[\"missing\"],\"weight\":1}]}',
    r'{\"schemaVersion\":2,\"id\":\"org.biocore.invalid\",\"name\":\"P\",\"version\":\"1.0.0\",\"steps\":[{\"id\":\"a\",\"module\":\"org.biocore.demo.validate\",\"pluginVersion\":\"0.1.0\",\"dependsOn\":[\"missing\"],\"weight\":1}]}',
)
json_io = json_io.replace('        "p",\n        "P",\n        "1",', '        "org.biocore.p",\n        "P",\n        "1.0.0",')
json_io = json_io.replace(
    'document.steps[0].plugin_version != "0.1.0" ||\n        document.steps[0].module_type',
    'document.steps[0].plugin_version != "0.1.0" ||\n        document.steps[0].plugin_manifest_version != 2U ||\n        document.steps[0].plugin_api_version != "1.0" ||\n        document.steps[0].module_type',
)
write("tests/json_pipeline_io_tests.cpp", json_io)

# Execution-plan fixture expects new metadata; auto-injected PipelineStep pins are already present.
execution_tests = read("tests/execution_plan_tests.cpp")
execution_tests = execution_tests.replace(
    '        plan.steps()[0].plugin_version != "0.1.0" ||\n        plan.steps()[0].executable_path.empty()',
    '        plan.steps()[0].plugin_version != "0.1.0" ||\n        plan.steps()[0].plugin_manifest_version != 2U ||\n        plan.steps()[0].plugin_api_version != "1.0" ||\n        plan.steps()[0].executable_path.empty()',
)
write("tests/execution_plan_tests.cpp", execution_tests)

write("tests/plugin_pipeline_contract_hardening_tests.cpp", r'''#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/i_plugin_registry.hpp"
#include "biocore/application/pipeline_planner.hpp"
#include "biocore/domain/pipeline_definition.hpp"
#include "biocore/domain/pipeline_step.hpp"
#include "biocore/pipeline_protocol/pipeline_document.hpp"
#include "biocore/pipeline_protocol/pipeline_document_codec.hpp"

namespace {

class PinnedRegistry final : public biocore::application::IPluginRegistry {
public:
    std::string version{"0.1.0"};
    std::uint32_t manifest_version{2U};
    std::string api_version{"1.0"};

    [[nodiscard]] std::optional<biocore::application::ResolvedPluginModule> find_module(
        const std::string_view module_id
    ) const override {
        if (module_id != "org.biocore.demo.validate") return std::nullopt;
        return biocore::application::ResolvedPluginModule{
            .plugin_id = "org.biocore.demo",
            .plugin_version = version,
            .plugin_manifest_version = manifest_version,
            .plugin_api_version = api_version,
            .module_id = "org.biocore.demo.validate",
            .module_type = biocore::domain::PluginModuleType::process,
            .plugin_root_path = "/plugins/org.biocore.demo",
            .executable_path = "/plugins/org.biocore.demo/bin/demo",
            .parameters = {}, .inputs = {}, .outputs = {},
        };
    }

    [[nodiscard]] std::vector<biocore::application::RegisteredPlugin> list_plugins() const override {
        return {};
    }
};

template <typename Function>
[[nodiscard]] bool rejects(Function&& function) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

[[nodiscard]] biocore::domain::PipelineDefinition definition(std::string plugin_version) {
    return biocore::domain::PipelineDefinition{
        2U,
        "org.biocore.demo.validation",
        "Pinned plugin validation",
        "0.2.0",
        {biocore::domain::PipelineStep{
            "validate", "org.biocore.demo.validate", std::move(plugin_version), {}, 1.0
        }},
    };
}

[[nodiscard]] bool exact_pin_contract() {
    PinnedRegistry registry;
    const auto plan = biocore::application::PipelinePlanner::create_execution_plan(
        definition("0.1.0"), "job-contract", 3, registry
    );
    return plan.schema_version() == 4U && plan.steps().size() == 1U &&
           plan.steps()[0].plugin_version == "0.1.0" &&
           plan.steps()[0].plugin_manifest_version == 2U &&
           plan.steps()[0].plugin_api_version == "1.0";
}

[[nodiscard]] bool mismatch_rejection_contract() {
    PinnedRegistry registry;
    const bool version_mismatch = rejects([&] {
        static_cast<void>(biocore::application::PipelinePlanner::create_execution_plan(
            definition("0.2.0"), "job-version-mismatch", 0, registry
        ));
    });
    registry.api_version = "2.0";
    const bool api_mismatch = rejects([&] {
        static_cast<void>(biocore::application::PipelinePlanner::create_execution_plan(
            definition("0.1.0"), "job-api-mismatch", 0, registry
        ));
    });
    registry.api_version = "1.0";
    registry.manifest_version = 99U;
    const bool manifest_mismatch = rejects([&] {
        static_cast<void>(biocore::application::PipelinePlanner::create_execution_plan(
            definition("0.1.0"), "job-manifest-mismatch", 0, registry
        ));
    });
    return version_mismatch && api_mismatch && manifest_mismatch;
}

[[nodiscard]] bool identity_contract() {
    return rejects([] {
               static_cast<void>(biocore::domain::PipelineDefinition{
                   2U, "Bad Pipeline", "Bad", "1.0.0",
                   {biocore::domain::PipelineStep{"a", "org.biocore.demo.validate", "0.1.0", {}, 1.0}}
               });
           }) &&
           rejects([] {
               static_cast<void>(biocore::domain::PipelineDefinition{
                   2U, "org.biocore.bad", "Bad", "1",
                   {biocore::domain::PipelineStep{"a", "org.biocore.demo.validate", "0.1.0", {}, 1.0}}
               });
           }) &&
           rejects([] {
               static_cast<void>(biocore::domain::PipelineStep{
                   "a", "not a module", "0.1.0", {}, 1.0
               });
           });
}

[[nodiscard]] bool document_round_trip_contract() {
    using namespace biocore::pipeline_protocol;
    const PipelineDefinitionDocument definition_document{
        .schema_version = current_pipeline_definition_schema_version,
        .id = "org.biocore.demo.validation",
        .name = "Pinned",
        .version = "0.2.0",
        .steps = {PipelineStepDocument{
            "validate", "org.biocore.demo.validate", "0.1.0", {}, 1.0
        }},
    };
    const auto parsed_definition = parse_pipeline_definition_document(
        serialize_pipeline_definition_document(definition_document)
    );
    if (parsed_definition.steps[0].plugin_version != "0.1.0") return false;

    const ExecutionPlanDocument plan_document{
        .schema_version = current_execution_plan_schema_version,
        .job_id = "job-doc",
        .job_revision = 1,
        .pipeline_id = "org.biocore.demo.validation",
        .pipeline_version = "0.2.0",
        .steps = {ExecutionPlanStepDocument{
            .id = "validate",
            .module_id = "org.biocore.demo.validate",
            .plugin_id = "org.biocore.demo",
            .plugin_version = "0.1.0",
            .plugin_manifest_version = 2U,
            .plugin_api_version = "1.0",
            .module_type = "process",
            .plugin_root_path = "/plugins/org.biocore.demo",
            .executable_path = "/plugins/org.biocore.demo/bin/demo",
            .depends_on = {},
            .weight = 1.0,
            .parameters = {}, .inputs = {}, .outputs = {},
        }},
    };
    const auto parsed_plan = parse_execution_plan_document(
        serialize_execution_plan_document(plan_document)
    );
    return parsed_plan.steps[0].plugin_manifest_version == 2U &&
           parsed_plan.steps[0].plugin_api_version == "1.0";
}

}  // namespace

int main() {
    if (!exact_pin_contract() || !mismatch_rejection_contract() ||
        !identity_contract() || !document_round_trip_contract()) {
        std::cerr << "Plugin/pipeline contract hardening tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Plugin/pipeline contract hardening tests passed\n";
    return EXIT_SUCCESS;
}
''')

replace_once(
    "CMakeLists.txt",
    "    add_test(\n        NAME integration.managed_file_integrity\n        COMMAND biocore-managed-file-integrity-tests\n    )\nendif()",
    "    add_test(\n        NAME integration.managed_file_integrity\n        COMMAND biocore-managed-file-integrity-tests\n    )\n\n    add_executable(\n        biocore-plugin-pipeline-contract-hardening-tests\n        tests/plugin_pipeline_contract_hardening_tests.cpp\n    )\n    target_link_libraries(\n        biocore-plugin-pipeline-contract-hardening-tests\n        PRIVATE BioCore::infrastructure BioCore::project_warnings BioCore::sanitizers\n    )\n    add_test(\n        NAME integration.plugin_pipeline_contract_hardening\n        COMMAND biocore-plugin-pipeline-contract-hardening-tests\n    )\nendif()",
)

replace_once(
    ".github/workflows/v0.2-validation.yml",
    '  BIOCORE_ITERATION: "049"\n  BIOCORE_TEST_FLOOR: "71"',
    '  BIOCORE_ITERATION: "050"\n  BIOCORE_TEST_FLOOR: "72"',
)
replace_once(
    ".github/workflows/v0.2-validation.yml",
    "      - name: Guard development preset identities\n        if: matrix.preset == 'linux-gcc-debug'",
    "      - name: Verify exact pipeline plugin-version pins\n        if: matrix.preset == 'linux-gcc-debug'\n        shell: bash\n        run: |\n          python3 - <<'PY'\n          import json\n          from pathlib import Path\n\n          module_versions = {}\n          for manifest_path in sorted(Path('plugins').glob('*/plugin.json')):\n              manifest = json.loads(manifest_path.read_text(encoding='utf-8'))\n              for module in manifest['modules']:\n                  module_versions[module['id']] = manifest['version']\n          pipelines = sorted(Path('pipelines').glob('*.json'))\n          if len(pipelines) != 12:\n              raise SystemExit(f'Expected 12 bundled pipelines; found {len(pipelines)}')\n          for path in pipelines:\n              document = json.loads(path.read_text(encoding='utf-8'))\n              if document.get('schemaVersion') != 2:\n                  raise SystemExit(f'{path}: expected pipeline schema v2')\n              for step in document['steps']:\n                  expected = module_versions.get(step['module'])\n                  if expected is None or step.get('pluginVersion') != expected:\n                      raise SystemExit(f\"{path}: plugin pin mismatch for {step['module']}\")\n          print('Pipeline plugin-version pin contract PASS')\n          PY\n\n      - name: Guard development preset identities\n        if: matrix.preset == 'linux-gcc-debug'",
)

write("docs/development/ITERATION-050.md", """# OpenGenesis-BioCore v0.2.0-dev — Iteration 050\n\n## Title\n\nPlugin & Pipeline Contract Hardening\n\n## Goal\n\nMake pipeline-to-plugin resolution exact and reproducible by eliminating silent plugin-version drift and carrying the resolved plugin contract into immutable execution-plan snapshots.\n\nProject database schema remains v8. Worker Protocol remains v2. Pipeline Definition schema advances to v2 and Execution Plan schema advances to v4.\n\n## Intended changes\n\n- require canonical namespaced pipeline identifiers and semantic pipeline versions;\n- require canonical namespaced module identifiers and exact semantic plugin-version pins on every pipeline step;\n- advance bundled pipeline documents to schema v2 with `pluginVersion` on every step;\n- reject planning when the discovered module version differs from the pipeline pin;\n- expose plugin manifest version and API version through the registry contract;\n- snapshot plugin manifest/API identity into execution-plan schema v4;\n- revalidate plugin namespace, semantic versions, supported manifest range, and API version when reconstructing an execution plan;\n- preserve existing plugin entrypoint canonicalization, no-symlink rules, platform selection, I/O contracts, and out-of-process execution;\n- add a dedicated hardening CTest and raise the Linux matrix floor from 71 to 72;\n- add a CI guard proving all 12 bundled pipelines pin the exact version declared by their shipped plugin manifest.\n\n## Explicit non-goals\n\nIteration 050 must not change project DB schema v8, Worker Protocol v2, job retry/failure semantics, managed-file integrity behavior, biological algorithms, scientific thresholds, loopback security boundaries, or process-tree supervision. It does not introduce plugin dependency solving, version ranges, remote registries, or dynamic plugin installation.\n\n## Acceptance criteria\n\n1. Pipeline Definition schema v2 requires an exact semantic `pluginVersion` per step.\n2. All 12 bundled pipelines use schema v2 and match their shipped plugin manifest version exactly.\n3. Pipeline ids/module ids use canonical namespaced identifiers and pipeline/plugin versions use semantic versioning.\n4. Planning rejects unavailable modules, plugin-version mismatches, unsupported manifest versions, unsupported API versions, or namespace mismatches.\n5. Execution Plan schema v4 records plugin id/version, manifest version, API version, module type, plugin root, and executable path.\n6. Execution-plan parsing/reconstruction rejects malformed or unsupported plugin contract metadata.\n7. Existing parameter/input/output binding contracts and immutable execution-plan snapshot semantics are preserved.\n8. Active CTest floor is at least 72 and GCC Debug, GCC Release, Clang Debug, and GCC ASan+UBSan all pass.\n9. Development identity remains exactly `0.2.0-dev`.\n10. Gemini review package is exactly four Markdown parts and Iteration 050 remains open until independent `ACCEPT`.\n""")

# Clean staging-only material so the candidate tree contains no transformer machinery.
for relative in ["scripts/finalize-iteration-050.py", ".github/workflows/iteration-050-apply.yml"]:
    target = ROOT / relative
    if target.exists():
        target.unlink()

print("Iteration 050 transformation complete")
