#include "biocore/domain/component_identity.hpp"

#include <algorithm>

namespace biocore::domain {
namespace {

[[nodiscard]] bool is_identifier_character(const char value) noexcept {
    return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
           value == '-' || value == '_';
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
            (!segment_has_character && (character == '-' || character == '_'))) {
            return false;
        }
        segment_has_character = true;
        previous_hyphen = character == '-' || character == '_';
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
