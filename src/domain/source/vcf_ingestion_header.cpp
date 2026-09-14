#include "vcf_ingestion_internal.hpp"

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace biocore::domain::vcf_detail {

[[nodiscard]] std::vector<std::string> parse_filters(const std::string_view raw, bool& applied) {
    if (raw == "PASS") {
        applied = true;
        return {};
    }
    if (raw == ".") {
        applied = false;
        return {};
    }
    applied = true;
    std::vector<std::string> filters;
    std::unordered_set<std::string> seen;
    for (const auto token : split(raw, ';')) {
        if (!safe_identifier(token) || token == "PASS" || token == ".") {
            throw std::invalid_argument("VCF FILTER token is invalid");
        }
        std::string value{token};
        if (!seen.insert(value).second) {
            throw std::invalid_argument("VCF FILTER token is duplicated");
        }
        filters.push_back(std::move(value));
    }
    return filters;
}

void parse_header_line(const std::string& line, VcfHeader& header) {
    if (line.starts_with("##INFO=")) {
        auto definition = parse_field_definition(line, "##INFO=");
        validate_reserved_definition(definition, false);
        const std::string id = definition.id;
        if (!header.info_definitions.emplace(id, std::move(definition)).second) {
            throw std::invalid_argument("VCF INFO definition is duplicated: " + id);
        }
    } else if (line.starts_with("##FORMAT=")) {
        auto definition = parse_field_definition(line, "##FORMAT=");
        validate_reserved_definition(definition, true);
        const std::string id = definition.id;
        if (!header.format_definitions.emplace(id, std::move(definition)).second) {
            throw std::invalid_argument("VCF FORMAT definition is duplicated: " + id);
        }
    }
}

}  // namespace biocore::domain::vcf_detail
