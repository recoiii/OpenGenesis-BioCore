#include "vcf_ingestion_internal.hpp"

#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace biocore::domain::vcf_detail {

void parse_info_fields(
    const std::string_view raw,
    const VcfHeader& header,
    VariantRecord& record
) {
    if (raw == ".") {
        return;
    }
    std::unordered_set<std::string> seen;
    for (const auto token : split(raw, ';')) {
        if (token.empty()) {
            throw std::invalid_argument("VCF INFO contains an empty field");
        }
        const auto equal = token.find('=');
        const std::string_view key_view = token.substr(0U, equal);
        if (!safe_identifier(key_view)) {
            throw std::invalid_argument("VCF INFO key is invalid");
        }
        std::string key{key_view};
        if (!seen.insert(key).second) {
            throw std::invalid_argument("VCF INFO key is duplicated: " + key);
        }
        VcfFieldDefinition fallback;
        const auto& definition = definition_for(header.info_definitions, key_view, false, fallback);
        VariantFieldValue value;
        if (definition.type == VcfFieldType::flag) {
            if (equal != std::string_view::npos) {
                throw std::invalid_argument("VCF INFO Flag field must not contain '=': " + key);
            }
            value = true;
        } else {
            if (equal == std::string_view::npos) {
                throw std::invalid_argument("VCF INFO non-Flag field requires '=': " + key);
            }
            value = parse_typed_value(
                token.substr(equal + 1U),
                definition,
                record.alternates.size(),
                std::nullopt,
                "VCF INFO " + key
            );
        }
        assign_info_field(record, std::move(key), std::move(value));
    }
}

}  // namespace biocore::domain::vcf_detail
