#include "biocore/domain/plugin_module_definition.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace biocore::domain {
namespace {

[[nodiscard]] bool is_identifier_character(const char value) noexcept {
    return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
           value == '-';
}

void require_identifier(const std::string_view value, const std::size_t maximum_length) {
    if (value.empty() || value.size() > maximum_length ||
        value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("Plugin module identifier is invalid");
    }
    bool segment_has_character = false;
    bool previous_hyphen = false;
    for (const char value_character : value) {
        if (value_character == '.') {
            if (!segment_has_character || previous_hyphen) {
                throw std::invalid_argument("Plugin module identifier is invalid");
            }
            segment_has_character = false;
            previous_hyphen = false;
            continue;
        }
        if (!is_identifier_character(value_character)) {
            throw std::invalid_argument("Plugin module identifier is invalid");
        }
        if (!segment_has_character && value_character == '-') {
            throw std::invalid_argument("Plugin module identifier is invalid");
        }
        segment_has_character = true;
        previous_hyphen = value_character == '-';
    }
    if (!segment_has_character || previous_hyphen) {
        throw std::invalid_argument("Plugin module identifier is invalid");
    }
}

void require_safe_relative_path(const std::string_view value) {
    if (value.empty() || value.size() > PluginModuleDefinition::maximum_relative_path_length ||
        value.find('\0') != std::string_view::npos || value.front() == '/' ||
        value.find('\\') != std::string_view::npos || value.find(':') != std::string_view::npos) {
        throw std::invalid_argument("Plugin entrypoint path is invalid");
    }

    std::size_t begin = 0U;
    while (begin < value.size()) {
        const std::size_t end = value.find('/', begin);
        const std::string_view segment = value.substr(
            begin, end == std::string_view::npos ? value.size() - begin : end - begin
        );
        if (segment.empty() || segment == "." || segment == "..") {
            throw std::invalid_argument("Plugin entrypoint path is unsafe");
        }
        if (std::ranges::any_of(segment, [](const char character) {
                return std::iscntrl(static_cast<unsigned char>(character)) != 0;
            })) {
            throw std::invalid_argument("Plugin entrypoint path contains control characters");
        }
        if (end == std::string_view::npos) break;
        begin = end + 1U;
    }
    if (value.back() == '/') {
        throw std::invalid_argument("Plugin entrypoint path is invalid");
    }
}

}  // namespace

std::string_view to_string(const PluginModuleType type) noexcept {
    switch (type) {
        case PluginModuleType::process: return "process";
    }
    return "unknown";
}

std::optional<PluginModuleType> plugin_module_type_from_string(
    const std::string_view value
) noexcept {
    if (value == "process") return PluginModuleType::process;
    return std::nullopt;
}

PluginModuleDefinition::PluginModuleDefinition(
    std::string id,
    const PluginModuleType type,
    std::vector<PluginEntrypoint> entrypoints,
    std::vector<PluginParameterDefinition> parameters,
    std::vector<PluginInputPortDefinition> inputs,
    std::vector<PluginOutputPortDefinition> outputs
)
    : id_{std::move(id)},
      type_{type},
      entrypoints_{std::move(entrypoints)},
      parameters_{std::move(parameters)},
      inputs_{std::move(inputs)},
      outputs_{std::move(outputs)} {
    require_identifier(id_, maximum_id_length);
    if (entrypoints_.empty() || entrypoints_.size() > maximum_entrypoints) {
        throw std::invalid_argument("Plugin module entrypoint count is invalid");
    }

    std::unordered_set<int> platforms;
    platforms.reserve(entrypoints_.size());
    for (const PluginEntrypoint& entrypoint : entrypoints_) {
        require_safe_relative_path(entrypoint.relative_path);
        if (!platforms.insert(static_cast<int>(entrypoint.platform)).second) {
            throw std::invalid_argument("Plugin module contains duplicate platform entrypoints");
        }
    }

    std::unordered_set<std::string_view> contract_names;
    contract_names.reserve(parameters_.size() + inputs_.size() + outputs_.size());
    for (const auto& parameter : parameters_) {
        if (!contract_names.insert(parameter.name()).second) {
            throw std::invalid_argument("Plugin module contract names must be unique");
        }
    }
    for (const auto& input : inputs_) {
        if (!contract_names.insert(input.name()).second) {
            throw std::invalid_argument("Plugin module contract names must be unique");
        }
    }
    for (const auto& output : outputs_) {
        if (!contract_names.insert(output.name()).second) {
            throw std::invalid_argument("Plugin module contract names must be unique");
        }
    }
}

std::string_view PluginModuleDefinition::id() const noexcept { return id_; }
PluginModuleType PluginModuleDefinition::type() const noexcept { return type_; }
const std::vector<PluginEntrypoint>& PluginModuleDefinition::entrypoints() const noexcept {
    return entrypoints_;
}

std::optional<std::string_view> PluginModuleDefinition::entrypoint_for(
    const PluginPlatform platform
) const noexcept {
    const auto iterator = std::ranges::find_if(entrypoints_, [platform](const auto& entrypoint) {
        return entrypoint.platform == platform;
    });
    if (iterator == entrypoints_.end()) return std::nullopt;
    return iterator->relative_path;
}

const std::vector<PluginParameterDefinition>& PluginModuleDefinition::parameters() const noexcept {
    return parameters_;
}
const std::vector<PluginInputPortDefinition>& PluginModuleDefinition::inputs() const noexcept {
    return inputs_;
}
const std::vector<PluginOutputPortDefinition>& PluginModuleDefinition::outputs() const noexcept {
    return outputs_;
}

}  // namespace biocore::domain
