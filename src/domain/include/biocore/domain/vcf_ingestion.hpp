#pragma once

#include "biocore/domain/genotype.hpp"
#include "biocore/domain/reference_genome.hpp"
#include "biocore/domain/variant_record.hpp"

#include <cstddef>
#include <cstdint>
#include <istream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace biocore::domain {

enum class VcfFieldType : std::uint8_t {
    integer,
    floating_point,
    flag,
    character,
    string
};

enum class VcfNumberKind : std::uint8_t {
    fixed,
    alternate,
    reference_and_alternate,
    genotype,
    variable
};

struct VcfFieldDefinition final {
    std::string id;
    VcfNumberKind number_kind{VcfNumberKind::variable};
    std::size_t fixed_count{0U};
    VcfFieldType type{VcfFieldType::string};
};

struct VcfHeader final {
    std::string file_format;
    std::unordered_map<std::string, VcfFieldDefinition> info_definitions;
    std::unordered_map<std::string, VcfFieldDefinition> format_definitions;
    std::vector<std::string> sample_names;
};

struct SampleVariantData final {
    bool genotype_present{false};
    GenotypeCall genotype;
    std::vector<DynamicVariantField> extra_format_fields;
};

struct CanonicalVariantRecord final {
    VariantRecord variant;
    std::string id;
    std::optional<double> quality;
    std::vector<std::string> filters;
    bool filters_applied{true};
    std::vector<SampleVariantData> samples;
};

struct VcfIngestionResult final {
    VcfHeader header;
    std::vector<CanonicalVariantRecord> records;
};

[[nodiscard]] VcfIngestionResult ingest_vcf(
    std::istream& input,
    const ReferenceGenome& reference
);

}  // namespace biocore::domain
