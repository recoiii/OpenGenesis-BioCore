#include "biocore/domain/variant_filtering.hpp"

#include <stdexcept>

namespace biocore::domain {

VariantFilterEvidence make_variant_filter_evidence(
    const VariantRecord& variant,
    const ProbabilisticGenotypeResult& genotype
) {
    if (const auto variant_error = validate_variant_record(variant); variant_error.has_value()) {
        throw std::invalid_argument(*variant_error);
    }
    const std::size_t allele_count = variant.alternates.size() + 1U;
    if (const auto genotype_error = validate_genotype_call(genotype.call, allele_count); genotype_error.has_value()) {
        throw std::invalid_argument(*genotype_error);
    }

    VariantFilterEvidence evidence;
    evidence.depth = genotype.call.depth;
    evidence.genotype_quality = genotype.call.genotype_quality;
    if (variant.info.mapping_quality.has_value()) {
        evidence.mapping_quality = static_cast<double>(*variant.info.mapping_quality);
    }
    evidence.alternate_depths.resize(variant.alternates.size());
    if (!genotype.call.allele_depths.empty()) {
        for (std::size_t index = 0U; index < variant.alternates.size(); ++index) {
            evidence.alternate_depths[index] = genotype.call.allele_depths[index + 1U];
        }
    }
    return evidence;
}

VariantFilterEvidence make_indel_filter_evidence(
    const IndelCandidate& candidate,
    const ProbabilisticGenotypeResult& genotype
) {
    VariantFilterEvidence evidence = make_variant_filter_evidence(candidate.variant, genotype);
    if (candidate.variant.alternates.size() != 1U) {
        throw std::invalid_argument("Iteration 057 indel candidate must be biallelic for filtering evidence adaptation");
    }
    const auto& source = candidate.evidence;
    const std::uint64_t classified = static_cast<std::uint64_t>(source.reference_support) +
        source.alternate_support + source.ambiguous_support;
    if (classified != source.informative_realignments) {
        throw std::invalid_argument("indel evidence realignment support is inconsistent");
    }
    if (static_cast<std::uint64_t>(source.alternate_forward) + source.alternate_reverse != source.alternate_support) {
        throw std::invalid_argument("indel alternate strand evidence does not equal alternate support");
    }

    evidence.alternate_strand.resize(1U);
    evidence.alternate_strand[0U] = AlternateStrandEvidence{
        source.alternate_forward,
        source.alternate_reverse
    };
    evidence.alternate_homopolymer.resize(1U);
    evidence.alternate_homopolymer[0U] = AlternateHomopolymerEvidence{
        source.homopolymer_run_length,
        source.homopolymer_base
    };
    if (!evidence.mapping_quality.has_value() && source.alternate_support > 0U) {
        evidence.mapping_quality = static_cast<double>(source.alternate_mapping_quality_sum) /
            static_cast<double>(source.alternate_support);
    }
    return evidence;
}

}  // namespace biocore::domain
