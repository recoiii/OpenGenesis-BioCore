#include "biocore/domain/variant_record.hpp"
#include "biocore/domain/variant_types.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

[[nodiscard]] bool require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "variant_types_tests: " << message << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main() {
    using biocore::domain::Allele;
    using biocore::domain::DynamicVariantField;
    using biocore::domain::NullableVariantList;
    using biocore::domain::VariantRecord;
    using biocore::domain::VariantType;
    using biocore::domain::classify_variant;
    using biocore::domain::internal_start_to_vcf_position;
    using biocore::domain::validate_variant_record;
    using biocore::domain::vcf_position_to_internal_start;

    bool ok = true;
    ok = require(classify_variant("A", "G") == VariantType::snv, "SNV classification") && ok;
    ok = require(classify_variant("AT", "CG") == VariantType::mnv, "MNV classification") && ok;
    ok = require(classify_variant("A", "ATG") == VariantType::insertion, "insertion classification") && ok;
    ok = require(classify_variant("ATG", "A") == VariantType::deletion, "deletion classification") && ok;
    ok = require(
        classify_variant("CTT", "GA") == VariantType::delins_complex,
        "DELINS/complex classification"
    ) && ok;
    ok = require(
        classify_variant("A", "<DEL>", true) == VariantType::unknown,
        "symbolic allele remains UNKNOWN"
    ) && ok;

    VariantRecord record;
    record.locus = {0U, 99U, 101U};
    record.reference = Allele{"AT", VariantType::unknown, false};
    record.alternates.push_back(Allele{"CG", VariantType::mnv, false});
    record.info.allele_frequencies = std::vector<float>{0.25F};
    record.info.allele_counts = std::vector<std::int32_t>{1};
    record.info.allele_number = 4;
    record.info.depth = 20;
    ok = require(!validate_variant_record(record).has_value(), "valid MNV record") && ok;
    ok = require(!record.alternates.uses_heap_storage(), "biallelic ALT stays inline") && ok;

    const NullableVariantList<std::int32_t> mixed_integer_list{
        std::int32_t{1}, std::nullopt, std::int32_t{3}
    };
    const DynamicVariantField mixed_field{"MIXED", mixed_integer_list};
    const auto* mixed = std::get_if<NullableVariantList<std::int32_t>>(&mixed_field.value);
    ok = require(mixed != nullptr, "typed Integer list preserved") && ok;
    if (mixed != nullptr) {
        ok = require(mixed->size() == 3U, "typed Integer list cardinality") && ok;
        ok = require((*mixed)[0].has_value() && *(*mixed)[0] == 1, "first Integer list value") && ok;
        ok = require(!(*mixed)[1].has_value(), "element-level missing Integer value") && ok;
        ok = require((*mixed)[2].has_value() && *(*mixed)[2] == 3, "third Integer list value") && ok;
    }

    record.alternates.push_back(Allele{"A", VariantType::deletion, false});
    ok = require(record.alternates.uses_heap_storage(), "multi-ALT spills safely") && ok;
    ok = require(validate_variant_record(record).has_value(), "AF/AC Number=A mismatch rejected") && ok;

    ok = require(vcf_position_to_internal_start(1U) == 0U, "VCF POS to internal start") && ok;
    ok = require(internal_start_to_vcf_position(0U) == 1U, "internal start to VCF POS") && ok;
    bool rejected_zero = false;
    try {
        static_cast<void>(vcf_position_to_internal_start(0U));
    } catch (const std::invalid_argument&) {
        rejected_zero = true;
    }
    ok = require(rejected_zero, "VCF POS zero rejected") && ok;
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
