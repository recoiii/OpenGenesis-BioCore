#include "biocore/domain/variant_filtering.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

namespace biocore::domain {
namespace {

[[nodiscard]] std::optional<double> compute_vaf(
    const VariantFilterEvidence& evidence,
    const std::size_t alternate_index
) noexcept {
    if (!evidence.depth.has_value() || *evidence.depth == 0U ||
        alternate_index >= evidence.alternate_depths.size() ||
        !evidence.alternate_depths[alternate_index].has_value()) {
        return std::nullopt;
    }
    return static_cast<double>(*evidence.alternate_depths[alternate_index]) /
        static_cast<double>(*evidence.depth);
}

[[nodiscard]] std::optional<double> compute_strand_fraction(
    const VariantFilterEvidence& evidence,
    const std::size_t alternate_index
) noexcept {
    if (alternate_index >= evidence.alternate_strand.size() ||
        !evidence.alternate_strand[alternate_index].has_value()) {
        return std::nullopt;
    }
    const auto& strand = *evidence.alternate_strand[alternate_index];
    const std::uint64_t total = static_cast<std::uint64_t>(strand.forward) + strand.reverse;
    if (total == 0U) {
        return std::nullopt;
    }
    const std::uint32_t minority = std::min(strand.forward, strand.reverse);
    return static_cast<double>(minority) / static_cast<double>(total);
}

void push_reason(VariantFilterDecision& decision, const VariantFilterReason reason) {
    decision.reasons.push_back(reason);
}

}  // namespace

VariantFilterDecision evaluate_variant_filter(
    const VariantRecord& variant,
    const VariantFilterEvidence& evidence,
    const std::size_t alternate_index,
    const AdvancedVariantFilterPolicy& policy
) {
    if (const auto variant_error = validate_variant_record(variant); variant_error.has_value()) {
        throw std::invalid_argument(*variant_error);
    }
    validate_advanced_variant_filter_policy(policy);
    if (const auto evidence_error = validate_variant_filter_evidence(variant, evidence); evidence_error.has_value()) {
        throw std::invalid_argument(std::string{*evidence_error});
    }
    if (alternate_index >= variant.alternates.size()) {
        throw std::out_of_range("alternate_index exceeds ALT cardinality");
    }

    VariantFilterDecision decision;
    decision.alternate_index = alternate_index;

    if (policy.minimum_depth.has_value()) {
        if (!evidence.depth.has_value()) {
            push_reason(decision, VariantFilterReason::missing_depth);
        } else if (*evidence.depth < *policy.minimum_depth) {
            push_reason(decision, VariantFilterReason::low_depth);
        }
    }

    if (policy.minimum_variant_allele_fraction.has_value()) {
        decision.variant_allele_fraction = compute_vaf(evidence, alternate_index);
        if (!decision.variant_allele_fraction.has_value()) {
            push_reason(decision, VariantFilterReason::missing_variant_allele_fraction);
        } else if (*decision.variant_allele_fraction < *policy.minimum_variant_allele_fraction) {
            push_reason(decision, VariantFilterReason::low_variant_allele_fraction);
        }
    }

    if (policy.minimum_genotype_quality.has_value()) {
        if (!evidence.genotype_quality.has_value()) {
            push_reason(decision, VariantFilterReason::missing_genotype_quality);
        } else if (*evidence.genotype_quality < *policy.minimum_genotype_quality) {
            push_reason(decision, VariantFilterReason::low_genotype_quality);
        }
    }

    if (policy.minimum_mapping_quality.has_value()) {
        if (!evidence.mapping_quality.has_value()) {
            push_reason(decision, VariantFilterReason::missing_mapping_quality);
        } else if (*evidence.mapping_quality < *policy.minimum_mapping_quality) {
            push_reason(decision, VariantFilterReason::low_mapping_quality);
        }
    }

    if (policy.minimum_alternate_strand_fraction.has_value()) {
        if (alternate_index >= evidence.alternate_strand.size() ||
            !evidence.alternate_strand[alternate_index].has_value()) {
            push_reason(decision, VariantFilterReason::missing_strand_evidence);
        } else {
            const auto& strand = *evidence.alternate_strand[alternate_index];
            const std::uint64_t strand_observations = static_cast<std::uint64_t>(strand.forward) + strand.reverse;
            decision.alternate_strand_fraction = compute_strand_fraction(evidence, alternate_index);
            if (strand_observations >= policy.minimum_strand_observations &&
                decision.alternate_strand_fraction.has_value() &&
                *decision.alternate_strand_fraction < *policy.minimum_alternate_strand_fraction) {
                push_reason(decision, VariantFilterReason::strand_bias);
            }
        }
    }

    if (policy.maximum_homopolymer_run_length.has_value()) {
        if (alternate_index >= evidence.alternate_homopolymer.size() ||
            !evidence.alternate_homopolymer[alternate_index].has_value()) {
            push_reason(decision, VariantFilterReason::missing_homopolymer_evidence);
        } else if (evidence.alternate_homopolymer[alternate_index]->run_length >
                   *policy.maximum_homopolymer_run_length) {
            push_reason(decision, VariantFilterReason::homopolymer_run);
        }
    }

    if (!policy.required_regions.empty() && !policy.required_regions.overlaps(variant.locus)) {
        push_reason(decision, VariantFilterReason::outside_required_region);
    }
    if (!policy.excluded_regions.empty() && policy.excluded_regions.overlaps(variant.locus)) {
        push_reason(decision, VariantFilterReason::excluded_region);
    }

    decision.passed = decision.reasons.empty();
    return decision;
}

std::string_view variant_filter_reason_name(const VariantFilterReason reason) noexcept {
    switch (reason) {
        case VariantFilterReason::missing_depth: return "MISSING_DP";
        case VariantFilterReason::low_depth: return "LOW_DP";
        case VariantFilterReason::missing_variant_allele_fraction: return "MISSING_VAF";
        case VariantFilterReason::low_variant_allele_fraction: return "LOW_VAF";
        case VariantFilterReason::missing_genotype_quality: return "MISSING_GQ";
        case VariantFilterReason::low_genotype_quality: return "LOW_GQ";
        case VariantFilterReason::missing_mapping_quality: return "MISSING_MQ";
        case VariantFilterReason::low_mapping_quality: return "LOW_MQ";
        case VariantFilterReason::missing_strand_evidence: return "MISSING_STRAND";
        case VariantFilterReason::strand_bias: return "STRAND_BIAS";
        case VariantFilterReason::missing_homopolymer_evidence: return "MISSING_HOMOPOLYMER";
        case VariantFilterReason::homopolymer_run: return "HOMOPOLYMER";
        case VariantFilterReason::outside_required_region: return "OUTSIDE_REGION";
        case VariantFilterReason::excluded_region: return "EXCLUDED_REGION";
    }
    return "UNKNOWN_FILTER";
}

}  // namespace biocore::domain
