#include "biocore/domain/variant_record.hpp"
#include "biocore/domain/variant_types.hpp"

#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <vector>

int main() {
    using biocore::domain::Allele;
    using biocore::domain::VariantRecord;
    using biocore::domain::VariantType;
    using biocore::domain::classify_variant;
    using biocore::domain::internal_start_to_vcf_position;
    using biocore::domain::validate_variant_record;
    using biocore::domain::vcf_position_to_internal_start;

    assert(classify_variant("A", "G") == VariantType::snv);
    assert(classify_variant("AT", "CG") == VariantType::mnv);
    assert(classify_variant("A", "ATG") == VariantType::insertion);
    assert(classify_variant("ATG", "A") == VariantType::deletion);
    assert(classify_variant("CTT", "GA") == VariantType::delins_complex);
    assert(classify_variant("A", "<DEL>", true) == VariantType::unknown);

    VariantRecord record;
    record.locus = {0U, 99U, 101U};
    record.reference = Allele{"AT", VariantType::unknown, false};
    record.alternates.push_back(Allele{"CG", VariantType::mnv, false});
    record.info.allele_frequencies = std::vector<float>{0.25F};
    record.info.allele_counts = std::vector<std::int32_t>{1};
    record.info.allele_number = 4;
    record.info.depth = 20;
    assert(!validate_variant_record(record).has_value());
    assert(!record.alternates.uses_heap_storage());

    record.alternates.push_back(Allele{"A", VariantType::deletion, false});
    assert(record.alternates.uses_heap_storage());
    assert(validate_variant_record(record).has_value());

    assert(vcf_position_to_internal_start(1U) == 0U);
    assert(internal_start_to_vcf_position(0U) == 1U);
    bool rejected_zero = false;
    try {
        static_cast<void>(vcf_position_to_internal_start(0U));
    } catch (const std::invalid_argument&) {
        rejected_zero = true;
    }
    assert(rejected_zero);
    return 0;
}
