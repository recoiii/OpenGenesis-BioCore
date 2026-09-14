#pragma once

#include "biocore/domain/vcf_ingestion.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace biocore::domain::vcf_detail {

[[nodiscard]] bool read_bounded_line(std::istream& input, std::string& line);
void strip_utf8_bom(std::string& line);
[[nodiscard]] std::vector<std::string_view> split(std::string_view value, char delimiter);
[[nodiscard]] std::int32_t parse_i32(std::string_view value, std::string_view label);
[[nodiscard]] std::uint64_t parse_u64(std::string_view value, std::string_view label);
[[nodiscard]] float parse_float(std::string_view value, std::string_view label);
[[nodiscard]] double parse_double(std::string_view value, std::string_view label);
[[nodiscard]] bool symbolic_alt(std::string_view value) noexcept;
void uppercase_allele(std::string& value);
[[nodiscard]] bool valid_sequence_allele(std::string_view value) noexcept;
[[nodiscard]] bool safe_identifier(std::string_view value) noexcept;
[[nodiscard]] std::unordered_map<std::string, std::string> parse_structured_attributes(
    std::string_view payload
);

[[nodiscard]] VcfFieldDefinition parse_field_definition(
    const std::string& line,
    std::string_view prefix
);
void validate_reserved_definition(const VcfFieldDefinition& definition, bool format);

[[nodiscard]] const VcfFieldDefinition& definition_for(
    const std::unordered_map<std::string, VcfFieldDefinition>& definitions,
    std::string_view key,
    bool format,
    VcfFieldDefinition& fallback
);
[[nodiscard]] std::optional<std::size_t> expected_cardinality(
    const VcfFieldDefinition& definition,
    std::size_t alt_count,
    std::optional<std::size_t> ploidy
);
[[nodiscard]] VariantFieldValue parse_typed_value(
    std::string_view raw,
    const VcfFieldDefinition& definition,
    std::size_t alt_count,
    std::optional<std::size_t> ploidy,
    std::string_view label
);
[[nodiscard]] bool all_present(const NullableVariantList<std::int32_t>& values) noexcept;
[[nodiscard]] bool all_present(const NullableVariantList<float>& values) noexcept;
void assign_info_field(VariantRecord& record, std::string key, VariantFieldValue value);

[[nodiscard]] CanonicalVariantRecord parse_record(
    const std::vector<std::string_view>& fields,
    const VcfHeader& header,
    const ReferenceGenome& reference
);

void parse_info_fields(std::string_view raw, const VcfHeader& header, VariantRecord& record);
[[nodiscard]] std::vector<SampleVariantData> parse_samples(
    const std::vector<std::string_view>& fields,
    const VcfHeader& header,
    std::size_t alt_count
);
[[nodiscard]] std::vector<std::string> parse_filters(std::string_view raw, bool& applied);
void parse_header_line(const std::string& line, VcfHeader& header);

}  // namespace biocore::domain::vcf_detail
