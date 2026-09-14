#include "biocore/domain/variant_filtering.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>

int main() {
    using namespace biocore::domain;
    VariantRecord variant;
    variant.locus = GenomicInterval{0U, 100U, 101U};
    variant.reference = Allele{"A", VariantType::unknown, false};
    variant.alternates.push_back(Allele{"G", VariantType::snv, false});
    variant.info.mapping_quality = 60.0F;

    const auto genotype = genotype_variant(variant, make_count_genotype_evidence({20U, 20U}, 0U, 0.01));
    VariantFilterEvidence evidence = make_variant_filter_evidence(variant, genotype);
    evidence.alternate_strand.resize(1U);
    evidence.alternate_strand[0U] = AlternateStrandEvidence{10U, 10U};
    evidence.alternate_homopolymer.resize(1U);
    evidence.alternate_homopolymer[0U] = AlternateHomopolymerEvidence{2U, 'A'};

    AdvancedVariantFilterPolicy policy;
    policy.minimum_depth = 30U;
    policy.minimum_variant_allele_fraction = 0.30;
    policy.minimum_genotype_quality = 20U;
    policy.minimum_mapping_quality = 40.0;
    policy.minimum_alternate_strand_fraction = 0.20;
    policy.minimum_strand_observations = 4U;
    policy.maximum_homopolymer_run_length = 8U;
    policy.required_regions = VariantRegionSet({GenomicInterval{0U, 0U, 1000U}});

    constexpr std::uint64_t evaluations = 1000000U;
    std::uint64_t passed = 0U;
    std::uint64_t checksum = 0U;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0U; index < evaluations; ++index) {
        const auto decision = evaluate_variant_filter(variant, evidence, 0U, policy);
        if (decision.passed) ++passed;
        if (decision.variant_allele_fraction.has_value()) {
            checksum += static_cast<std::uint64_t>(*decision.variant_allele_fraction * 1000.0);
        }
    }
    const auto end = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "evaluations=" << evaluations << '\n';
    std::cout << "passed=" << passed << '\n';
    std::cout << "reason_count=0\n";
    std::cout << "vaf_checksum=" << checksum << '\n';
    std::cout << "elapsed_ms=" << elapsed_ms << '\n';
    return passed == evaluations && checksum == 500000000U ? 0 : 1;
}
