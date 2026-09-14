#include "vcf_ingestion_internal.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace biocore::domain::vcf_detail {

template <typename T, typename Parser>
[[nodiscard]] NullableVariantList<T> parse_nullable_list(
    const std::vector<std::string_view>& tokens,
    Parser parser,
    const std::string_view label
) {
    NullableVariantList<T> values;
    values.reserve(tokens.size());
    for (const auto token : tokens) {
        if (token == ".") {
            values.push_back(std::nullopt);
        } else {
            values.push_back(parser(token, label));
        }
    }
    return values;
}

[[nodiscard]] VariantFieldValue parse_typed_value(
    const std::string_view raw,
    const VcfFieldDefinition& definition,
    const std::size_t alt_count,
    const std::optional<std::size_t> ploidy,
    const std::string_view label
) {
    if (raw == ".") {
        return MissingFieldValue{};
    }
    if (definition.type == VcfFieldType::flag) {
        throw std::invalid_argument(std::string{label} + " Flag value must not use '='");
    }

    const auto tokens = split(raw, ',');
    const auto expected = expected_cardinality(definition, alt_count, ploidy);
    if (expected.has_value() && tokens.size() != *expected) {
        throw std::invalid_argument(std::string{label} + " cardinality does not match Number");
    }
    const bool scalar = definition.number_kind == VcfNumberKind::fixed && definition.fixed_count == 1U;
    if (scalar && tokens.size() != 1U) {
        throw std::invalid_argument(std::string{label} + " must be scalar");
    }

    switch (definition.type) {
        case VcfFieldType::integer:
            if (scalar) return parse_i32(raw, label);
            return parse_nullable_list<std::int32_t>(tokens, parse_i32, label);
        case VcfFieldType::floating_point:
            if (scalar) return parse_float(raw, label);
            return parse_nullable_list<float>(tokens, parse_float, label);
        case VcfFieldType::character: {
            const auto parser = [](const std::string_view token, const std::string_view field) {
                if (token.size() != 1U) {
                    throw std::invalid_argument(std::string{field} + " Character value must contain exactly one character");
                }
                return token.front();
            };
            if (scalar) return parser(raw, label);
            return parse_nullable_list<char>(tokens, parser, label);
        }
        case VcfFieldType::string: {
            const auto parser = [](const std::string_view token, const std::string_view) {
                return std::string{token};
            };
            if (scalar) return std::string{raw};
            return parse_nullable_list<std::string>(tokens, parser, label);
        }
        case VcfFieldType::flag:
            break;
    }
    throw std::logic_error("unsupported VCF typed value state");
}

[[nodiscard]] bool all_present(const NullableVariantList<std::int32_t>& values) noexcept {
    return std::all_of(values.begin(), values.end(), [](const auto& value) { return value.has_value(); });
}

[[nodiscard]] bool all_present(const NullableVariantList<float>& values) noexcept {
    return std::all_of(values.begin(), values.end(), [](const auto& value) { return value.has_value(); });
}

void assign_info_field(VariantRecord& record, std::string key, VariantFieldValue value) {
    if (key == "AF") {
        if (const auto* values = std::get_if<NullableVariantList<float>>(&value); values != nullptr) {
            for (const auto& item : *values) {
                if (item.has_value() && (*item < 0.0F || *item > 1.0F)) {
                    throw std::invalid_argument("VCF INFO AF values must be within 0..1");
                }
            }
            if (all_present(*values)) {
                std::vector<float> fast;
                fast.reserve(values->size());
                for (const auto& item : *values) fast.push_back(*item);
                record.info.allele_frequencies = std::move(fast);
                return;
            }
        }
    } else if (key == "AC") {
        if (const auto* values = std::get_if<NullableVariantList<std::int32_t>>(&value); values != nullptr) {
            for (const auto& item : *values) {
                if (item.has_value() && *item < 0) {
                    throw std::invalid_argument("VCF INFO AC values must not be negative");
                }
            }
            if (all_present(*values)) {
                std::vector<std::int32_t> fast;
                fast.reserve(values->size());
                for (const auto& item : *values) fast.push_back(*item);
                record.info.allele_counts = std::move(fast);
                return;
            }
        }
    } else if (key == "AN") {
        if (const auto* scalar = std::get_if<std::int32_t>(&value)) {
            if (*scalar < 0) throw std::invalid_argument("VCF INFO AN must not be negative");
            record.info.allele_number = *scalar;
            return;
        }
    } else if (key == "DP") {
        if (const auto* scalar = std::get_if<std::int32_t>(&value)) {
            if (*scalar < 0) throw std::invalid_argument("VCF INFO DP must not be negative");
            record.info.depth = *scalar;
            return;
        }
    } else if (key == "MQ") {
        if (const auto* scalar = std::get_if<float>(&value)) {
            if (*scalar < 0.0F) throw std::invalid_argument("VCF INFO MQ must not be negative");
            record.info.mapping_quality = *scalar;
            return;
        }
    }
    record.info.extra_fields.push_back(DynamicVariantField{std::move(key), std::move(value)});
}

}  // namespace biocore::domain::vcf_detail
