#pragma once

#include "biocore/domain/contig_table.hpp"
#include "biocore/domain/inline_values.hpp"
#include "biocore/domain/variant_types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace biocore::domain {

struct GenomicInterval final {
    ContigId contig_id{0U};
    std::uint64_t start{0U};
    std::uint64_t end{0U};

    [[nodiscard]] bool valid() const noexcept { return start <= end; }
    [[nodiscard]] std::uint64_t length() const noexcept { return end - start; }
};

struct MissingFieldValue final {
    friend constexpr bool operator==(MissingFieldValue, MissingFieldValue) noexcept = default;
};

using VariantFieldValue = std::variant<
    MissingFieldValue,
    std::int32_t,
    float,
    char,
    std::string,
    bool,
    std::vector<std::int32_t>,
    std::vector<float>,
    std::vector<char>,
    std::vector<std::string>
>;

struct DynamicVariantField final {
    std::string key;
    VariantFieldValue value;
};

struct VariantInfo final {
    std::optional<std::vector<float>> allele_frequencies;
    std::optional<std::vector<std::int32_t>> allele_counts;
    std::optional<std::int32_t> allele_number;
    std::optional<std::int32_t> depth;
    std::optional<float> mapping_quality;
    std::vector<DynamicVariantField> extra_fields;
};

struct Allele final {
    std::string sequence;
    VariantType type{VariantType::unknown};
    bool symbolic{false};
};

struct VariantRecord final {
    GenomicInterval locus;
    Allele reference;
    InlineValues<Allele, 1U> alternates;
    VariantInfo info;
};

[[nodiscard]] std::optional<std::string> validate_variant_record(const VariantRecord& record);
[[nodiscard]] std::uint64_t vcf_position_to_internal_start(std::uint64_t vcf_position);
[[nodiscard]] std::uint64_t internal_start_to_vcf_position(std::uint64_t internal_start);

}  // namespace biocore::domain
