#include "vcf_ingestion_internal.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace biocore::domain::vcf_detail {

[[nodiscard]] VcfFieldType parse_field_type(const std::string_view value) {
    if (value == "Integer") return VcfFieldType::integer;
    if (value == "Float") return VcfFieldType::floating_point;
    if (value == "Flag") return VcfFieldType::flag;
    if (value == "Character") return VcfFieldType::character;
    if (value == "String") return VcfFieldType::string;
    throw std::invalid_argument("VCF field Type is unsupported: " + std::string{value});
}

[[nodiscard]] VcfFieldDefinition parse_field_definition(const std::string& line, const std::string_view prefix) {
    if (!line.starts_with(prefix) || line.size() <= prefix.size() + 1U || line[prefix.size()] != '<'
        || line.back() != '>') {
        throw std::invalid_argument("VCF field definition is malformed");
    }
    const std::string_view payload{line.data() + prefix.size() + 1U, line.size() - prefix.size() - 2U};
    const auto attributes = parse_structured_attributes(payload);
    const auto id_it = attributes.find("ID");
    const auto number_it = attributes.find("Number");
    const auto type_it = attributes.find("Type");
    if (id_it == attributes.end() || number_it == attributes.end() || type_it == attributes.end()
        || !safe_identifier(id_it->second)) {
        throw std::invalid_argument("VCF field definition is missing ID, Number or Type");
    }

    VcfFieldDefinition definition;
    definition.id = id_it->second;
    definition.type = parse_field_type(type_it->second);
    const std::string& number = number_it->second;
    if (number == "A") definition.number_kind = VcfNumberKind::alternate;
    else if (number == "R") definition.number_kind = VcfNumberKind::reference_and_alternate;
    else if (number == "G") definition.number_kind = VcfNumberKind::genotype;
    else if (number == ".") definition.number_kind = VcfNumberKind::variable;
    else {
        definition.number_kind = VcfNumberKind::fixed;
        definition.fixed_count = static_cast<std::size_t>(parse_u64(number, "VCF Number"));
    }
    if (definition.type == VcfFieldType::flag
        && (definition.number_kind != VcfNumberKind::fixed || definition.fixed_count != 0U)) {
        throw std::invalid_argument("VCF Flag fields must use Number=0");
    }
    return definition;
}

[[nodiscard]] VcfFieldDefinition reserved_info_definition(const std::string_view key) {
    if (key == "AF") return {"AF", VcfNumberKind::alternate, 0U, VcfFieldType::floating_point};
    if (key == "AC") return {"AC", VcfNumberKind::alternate, 0U, VcfFieldType::integer};
    if (key == "AN") return {"AN", VcfNumberKind::fixed, 1U, VcfFieldType::integer};
    if (key == "DP") return {"DP", VcfNumberKind::fixed, 1U, VcfFieldType::integer};
    if (key == "MQ") return {"MQ", VcfNumberKind::fixed, 1U, VcfFieldType::floating_point};
    return {};
}

[[nodiscard]] VcfFieldDefinition reserved_format_definition(const std::string_view key) {
    if (key == "GT") return {"GT", VcfNumberKind::fixed, 1U, VcfFieldType::string};
    if (key == "AD") return {"AD", VcfNumberKind::reference_and_alternate, 0U, VcfFieldType::integer};
    if (key == "DP") return {"DP", VcfNumberKind::fixed, 1U, VcfFieldType::integer};
    if (key == "GQ") return {"GQ", VcfNumberKind::fixed, 1U, VcfFieldType::integer};
    if (key == "PL") return {"PL", VcfNumberKind::genotype, 0U, VcfFieldType::integer};
    return {};
}

[[nodiscard]] bool definitions_equal(const VcfFieldDefinition& left, const VcfFieldDefinition& right) noexcept {
    return left.number_kind == right.number_kind
        && left.fixed_count == right.fixed_count
        && left.type == right.type;
}

void validate_reserved_definition(const VcfFieldDefinition& definition, const bool format) {
    const VcfFieldDefinition expected = format
        ? reserved_format_definition(definition.id)
        : reserved_info_definition(definition.id);
    if (!expected.id.empty() && !definitions_equal(definition, expected)) {
        throw std::invalid_argument("VCF reserved field definition conflicts with its canonical type/cardinality: " + definition.id);
    }
}

[[nodiscard]] const VcfFieldDefinition& definition_for(
    const std::unordered_map<std::string, VcfFieldDefinition>& definitions,
    const std::string_view key,
    const bool format,
    VcfFieldDefinition& fallback
) {
    const auto found = definitions.find(std::string{key});
    if (found != definitions.end()) {
        return found->second;
    }
    fallback = format ? reserved_format_definition(key) : reserved_info_definition(key);
    if (fallback.id.empty()) {
        throw std::invalid_argument("VCF field is used without a declared definition: " + std::string{key});
    }
    return fallback;
}

[[nodiscard]] std::optional<std::size_t> expected_cardinality(
    const VcfFieldDefinition& definition,
    const std::size_t alt_count,
    const std::optional<std::size_t> ploidy
) {
    switch (definition.number_kind) {
        case VcfNumberKind::fixed:
            return definition.fixed_count;
        case VcfNumberKind::alternate:
            return alt_count;
        case VcfNumberKind::reference_and_alternate:
            return alt_count + 1U;
        case VcfNumberKind::genotype:
            if (!ploidy.has_value()) return std::nullopt;
            return genotype_likelihood_count(alt_count + 1U, *ploidy);
        case VcfNumberKind::variable:
            return std::nullopt;
    }
    return std::nullopt;
}

}  // namespace biocore::domain::vcf_detail
