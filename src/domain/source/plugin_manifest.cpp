#include "biocore/domain/plugin_manifest.hpp"

#include <algorithm>
#include <cctype>
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
    const std::string_view field,
    const std::size_t maximum_length
) {
    if (is_blank(value) || value.size() > maximum_length ||
        value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string{field} + " is invalid");
    }
}

[[nodiscard]] bool is_identifier_character(const char value) noexcept {
    return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
           value == '-';
}

void require_identifier(const std::string_view value) {
    if (value.empty() || value.size() > PluginManifest::maximum_id_length ||
        value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("Plugin identifier is invalid");
    }
    bool segment_has_character = false;
    bool previous_hyphen = false;
    for (const char character : value) {
        if (character == '.') {
            if (!segment_has_character || previous_hyphen) {
                throw std::invalid_argument("Plugin identifier is invalid");
            }
            segment_has_character = false;
            previous_hyphen = false;
            continue;
        }
        if (!is_identifier_character(character) || (!segment_has_character && character == '-')) {
            throw std::invalid_argument("Plugin identifier is invalid");
        }
        segment_has_character = true;
        previous_hyphen = character == '-';
    }
    if (!segment_has_character || previous_hyphen) {
        throw std::invalid_argument("Plugin identifier is invalid");
    }
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

[[nodiscard]] bool is_semantic_version(const std::string_view value) noexcept {
    if (value.empty()) return false;

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

}  // namespace

PluginManifest::PluginManifest(
    const std::uint32_t manifest_version,
    std::string id,
    std::string name,
    std::string version,
    std::string api_version,
    std::string publisher,
    std::vector<PluginModuleDefinition> modules
)
    : manifest_version_{manifest_version},
      id_{std::move(id)},
      name_{std::move(name)},
      version_{std::move(version)},
      api_version_{std::move(api_version)},
      publisher_{std::move(publisher)},
      modules_{std::move(modules)} {
    if (manifest_version_ < minimum_manifest_version ||
        manifest_version_ > current_manifest_version) {
        throw std::invalid_argument("Plugin manifest version is unsupported");
    }
    require_identifier(id_);
    require_text(name_, "Plugin name", maximum_name_length);
    require_text(version_, "Plugin version", maximum_version_length);
    if (!is_semantic_version(version_)) {
        throw std::invalid_argument("Plugin version must use semantic versioning");
    }
    require_text(api_version_, "Plugin API version", maximum_api_version_length);
    if (api_version_ != supported_api_version) {
        throw std::invalid_argument("Plugin API version is unsupported");
    }
    require_text(publisher_, "Plugin publisher", maximum_publisher_length);
    if (modules_.empty() || modules_.size() > maximum_modules) {
        throw std::invalid_argument("Plugin module count is invalid");
    }

    const std::string prefix = id_ + '.';
    std::unordered_set<std::string_view> module_ids;
    module_ids.reserve(modules_.size());
    for (const PluginModuleDefinition& module : modules_) {
        if (manifest_version_ == 1U &&
            (!module.parameters().empty() || !module.inputs().empty() || !module.outputs().empty())) {
            throw std::invalid_argument("Plugin manifest v1 cannot contain I/O contracts");
        }
        if (!module.id().starts_with(prefix)) {
            throw std::invalid_argument("Plugin module identifier is outside the plugin namespace");
        }
        if (!module_ids.insert(module.id()).second) {
            throw std::invalid_argument("Plugin contains duplicate module identifiers");
        }
    }
}

std::uint32_t PluginManifest::manifest_version() const noexcept { return manifest_version_; }
std::string_view PluginManifest::id() const noexcept { return id_; }
std::string_view PluginManifest::name() const noexcept { return name_; }
std::string_view PluginManifest::version() const noexcept { return version_; }
std::string_view PluginManifest::api_version() const noexcept { return api_version_; }
std::string_view PluginManifest::publisher() const noexcept { return publisher_; }
const std::vector<PluginModuleDefinition>& PluginManifest::modules() const noexcept {
    return modules_;
}

}  // namespace biocore::domain
