#pragma once

#include "biocore/application/variant_export.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <limits>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace biocore::presentation::variant_export_support {

using domain::AssociationOddsRatio;
using domain::AssociationOddsRatioKind;
using domain::ReferenceAssembly;
using domain::ReferenceAssemblyIdentity;

[[nodiscard]] inline std::string_view assembly_name(const ReferenceAssemblyIdentity& identity) noexcept {
    switch (identity.assembly) {
        case ReferenceAssembly::unspecified: return "unspecified";
        case ReferenceAssembly::grch37: return "GRCh37";
        case ReferenceAssembly::grch38: return "GRCh38";
        case ReferenceAssembly::custom: return "custom";
    }
    return "unspecified";
}

[[nodiscard]] inline std::string assembly_label(const ReferenceAssemblyIdentity& identity) {
    return identity.assembly == ReferenceAssembly::custom
        ? std::string{"custom:"} + identity.custom_id
        : std::string{assembly_name(identity)};
}

[[nodiscard]] inline std::string_view odds_kind(const AssociationOddsRatioKind kind) noexcept {
    switch (kind) {
        case AssociationOddsRatioKind::undefined: return "undefined";
        case AssociationOddsRatioKind::zero: return "zero";
        case AssociationOddsRatioKind::finite: return "finite";
        case AssociationOddsRatioKind::positive_infinity: return "positive_infinity";
    }
    return "undefined";
}

[[nodiscard]] inline std::optional<double> odds_value(const AssociationOddsRatio& value) noexcept {
    if (value.kind == AssociationOddsRatioKind::zero) return 0.0;
    if (value.kind == AssociationOddsRatioKind::finite) return value.value;
    return std::nullopt;
}

inline void write_double(std::ostream& output, const double value) {
    const auto flags = output.flags();
    const auto precision = output.precision();
    output << std::setprecision(17) << value;
    output.flags(flags);
    output.precision(precision);
}

inline void write_optional_double(
    std::ostream& output,
    const std::optional<double>& value,
    const std::string_view missing = "."
) {
    if (value.has_value()) write_double(output, *value); else output << missing;
}

[[nodiscard]] inline std::string json_escape(const std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 8U);
    constexpr std::array<char, 16U> hex{
        '0','1','2','3','4','5','6','7','8','9','a','b','c','d','e','f'};
    for (const char raw : value) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (c < 0x20U) {
                    escaped += "\\u00";
                    escaped += hex[(c >> 4U) & 0x0fU];
                    escaped += hex[c & 0x0fU];
                } else {
                    escaped.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return escaped;
}

inline void write_json_string(std::ostream& output, const std::string_view value) {
    output << '"' << json_escape(value) << '"';
}

inline void write_json_optional_string(
    std::ostream& output,
    const std::optional<std::string>& value
) {
    if (value.has_value()) write_json_string(output, *value); else output << "null";
}

inline void write_json_optional_double(
    std::ostream& output,
    const std::optional<double>& value
) {
    if (value.has_value()) write_double(output, *value); else output << "null";
}

[[nodiscard]] inline std::string percent_encode(const std::string_view value) {
    constexpr std::array<char, 16U> hex{
        '0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'};
    std::string encoded;
    for (const char raw : value) {
        const auto c = static_cast<unsigned char>(raw);
        if (std::isalnum(c) != 0 || raw == '.' || raw == '_' || raw == '-' || raw == ':') {
            encoded.push_back(raw);
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[(c >> 4U) & 0x0fU]);
            encoded.push_back(hex[c & 0x0fU]);
        }
    }
    return encoded;
}

[[nodiscard]] inline bool vcf_identifier_safe(const std::string_view value) noexcept {
    return !value.empty() && value != "." && std::ranges::all_of(value, [](const char character) {
        const auto c = static_cast<unsigned char>(character);
        return std::isalnum(c) != 0 || character == '.' || character == '_' ||
               character == '-' || character == ':' || character == ';';
    });
}

[[nodiscard]] inline bool vcf_contig_safe(const std::string_view value) noexcept {
    return !value.empty() && std::ranges::none_of(value, [](const char character) {
        const auto c = static_cast<unsigned char>(character);
        return std::isspace(c) != 0 || character == ':' || character == '[' || character == ']';
    });
}

inline void require_stream(std::ostream& output) {
    if (!output) throw std::runtime_error("Variant export stream write failed");
}

}  // namespace biocore::presentation::variant_export_support
