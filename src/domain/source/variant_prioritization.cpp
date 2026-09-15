#include "biocore/domain/variant_prioritization.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace biocore::domain {
namespace {

[[nodiscard]] bool is_numeric_feature(const PrioritizationFeature feature) noexcept {
    switch (feature) {
        case PrioritizationFeature::depth:
        case PrioritizationFeature::variant_allele_fraction:
        case PrioritizationFeature::genotype_quality:
        case PrioritizationFeature::mapping_quality:
        case PrioritizationFeature::alternate_strand_fraction:
        case PrioritizationFeature::homopolymer_run_length:
            return true;
        case PrioritizationFeature::filter_passed:
        case PrioritizationFeature::variant_type:
            return false;
    }
    return false;
}

[[nodiscard]] bool is_equality_operator(const PrioritizationOperator comparison) noexcept {
    return comparison == PrioritizationOperator::equal || comparison == PrioritizationOperator::not_equal;
}

[[nodiscard]] std::optional<PrioritizationValue> feature_value(
    const VariantPrioritizationContext& context,
    const PrioritizationFeature feature
) {
    switch (feature) {
        case PrioritizationFeature::depth:
            if (context.depth.has_value()) return PrioritizationValue{*context.depth};
            break;
        case PrioritizationFeature::variant_allele_fraction:
            if (context.variant_allele_fraction.has_value()) return PrioritizationValue{*context.variant_allele_fraction};
            break;
        case PrioritizationFeature::genotype_quality:
            if (context.genotype_quality.has_value()) return PrioritizationValue{*context.genotype_quality};
            break;
        case PrioritizationFeature::mapping_quality:
            if (context.mapping_quality.has_value()) return PrioritizationValue{*context.mapping_quality};
            break;
        case PrioritizationFeature::alternate_strand_fraction:
            if (context.alternate_strand_fraction.has_value()) return PrioritizationValue{*context.alternate_strand_fraction};
            break;
        case PrioritizationFeature::homopolymer_run_length:
            if (context.homopolymer_run_length.has_value()) return PrioritizationValue{*context.homopolymer_run_length};
            break;
        case PrioritizationFeature::filter_passed:
            return PrioritizationValue{context.filter_passed};
        case PrioritizationFeature::variant_type:
            return PrioritizationValue{context.variant_type};
    }
    return std::nullopt;
}

[[nodiscard]] bool compare_numeric(
    const double observed,
    const double expected,
    const PrioritizationOperator comparison
) noexcept {
    switch (comparison) {
        case PrioritizationOperator::equal: return observed == expected;
        case PrioritizationOperator::not_equal: return observed != expected;
        case PrioritizationOperator::less_than: return observed < expected;
        case PrioritizationOperator::less_or_equal: return observed <= expected;
        case PrioritizationOperator::greater_than: return observed > expected;
        case PrioritizationOperator::greater_or_equal: return observed >= expected;
    }
    return false;
}

[[nodiscard]] bool rule_matches(
    const PrioritizationValue& observed,
    const VariantPrioritizationRule& rule
) {
    if (const auto* observed_number = std::get_if<double>(&observed)) {
        const auto* expected_number = std::get_if<double>(&rule.expected);
        if (expected_number == nullptr) throw std::logic_error("validated numeric prioritization rule changed type");
        return compare_numeric(*observed_number, *expected_number, rule.comparison);
    }
    if (const auto* observed_bool = std::get_if<bool>(&observed)) {
        const auto* expected_bool = std::get_if<bool>(&rule.expected);
        if (expected_bool == nullptr) throw std::logic_error("validated boolean prioritization rule changed type");
        const bool equal = *observed_bool == *expected_bool;
        return rule.comparison == PrioritizationOperator::equal ? equal : !equal;
    }
    const auto* observed_type = std::get_if<VariantType>(&observed);
    const auto* expected_type = std::get_if<VariantType>(&rule.expected);
    if (observed_type == nullptr || expected_type == nullptr) {
        throw std::logic_error("validated variant-type prioritization rule changed type");
    }
    const bool equal = *observed_type == *expected_type;
    return rule.comparison == PrioritizationOperator::equal ? equal : !equal;
}

[[nodiscard]] std::optional<double> alternate_strand_fraction(
    const VariantFilterEvidence& evidence,
    const std::size_t alternate_index
) noexcept {
    if (alternate_index >= evidence.alternate_strand.size() ||
        !evidence.alternate_strand[alternate_index].has_value()) {
        return std::nullopt;
    }
    const auto& strand = *evidence.alternate_strand[alternate_index];
    const std::uint64_t total = static_cast<std::uint64_t>(strand.forward) + strand.reverse;
    if (total == 0U) return std::nullopt;
    return static_cast<double>(std::min(strand.forward, strand.reverse)) / static_cast<double>(total);
}

}  // namespace

void validate_variant_prioritization_policy(const VariantPrioritizationPolicy& policy) {
    if (!std::isfinite(policy.baseline_score)) {
        throw std::invalid_argument("prioritization baseline score must be finite");
    }
    std::unordered_set<std::string> ids;
    ids.reserve(policy.rules.size());
    for (const auto& rule : policy.rules) {
        if (rule.id.empty()) throw std::invalid_argument("prioritization rule id must not be empty");
        if (!ids.insert(rule.id).second) throw std::invalid_argument("prioritization rule ids must be unique");
        if (!std::isfinite(rule.score_delta)) throw std::invalid_argument("prioritization score delta must be finite");

        if (is_numeric_feature(rule.feature)) {
            const auto* expected = std::get_if<double>(&rule.expected);
            if (expected == nullptr || !std::isfinite(*expected)) {
                throw std::invalid_argument("numeric prioritization rule requires a finite numeric expected value");
            }
        } else if (rule.feature == PrioritizationFeature::filter_passed) {
            if (!std::holds_alternative<bool>(rule.expected) || !is_equality_operator(rule.comparison)) {
                throw std::invalid_argument("filter-pass prioritization rule requires boolean equality comparison");
            }
        } else {
            if (!std::holds_alternative<VariantType>(rule.expected) || !is_equality_operator(rule.comparison)) {
                throw std::invalid_argument("variant-type prioritization rule requires VariantType equality comparison");
            }
        }
    }
}

VariantPrioritizationContext make_variant_prioritization_context(
    const VariantRecord& variant,
    const VariantFilterEvidence& evidence,
    const VariantFilterDecision& filter_decision
) {
    if (const auto variant_error = validate_variant_record(variant); variant_error.has_value()) {
        throw std::invalid_argument(*variant_error);
    }
    if (const auto evidence_error = validate_variant_filter_evidence(variant, evidence); evidence_error.has_value()) {
        throw std::invalid_argument(std::string{*evidence_error});
    }
    if (filter_decision.alternate_index >= variant.alternates.size()) {
        throw std::out_of_range("filter decision alternate index exceeds ALT cardinality");
    }

    const std::size_t alternate_index = filter_decision.alternate_index;
    VariantPrioritizationContext context;
    context.alternate_index = alternate_index;
    if (evidence.depth.has_value()) context.depth = static_cast<double>(*evidence.depth);
    context.variant_allele_fraction = filter_decision.variant_allele_fraction;
    if (!context.variant_allele_fraction.has_value() && evidence.depth.has_value() && *evidence.depth > 0U &&
        alternate_index < evidence.alternate_depths.size() && evidence.alternate_depths[alternate_index].has_value()) {
        context.variant_allele_fraction = static_cast<double>(*evidence.alternate_depths[alternate_index]) /
            static_cast<double>(*evidence.depth);
    }
    if (evidence.genotype_quality.has_value()) context.genotype_quality = static_cast<double>(*evidence.genotype_quality);
    context.mapping_quality = evidence.mapping_quality;
    context.alternate_strand_fraction = filter_decision.alternate_strand_fraction;
    if (!context.alternate_strand_fraction.has_value()) {
        context.alternate_strand_fraction = alternate_strand_fraction(evidence, alternate_index);
    }
    if (alternate_index < evidence.alternate_homopolymer.size() &&
        evidence.alternate_homopolymer[alternate_index].has_value()) {
        context.homopolymer_run_length = static_cast<double>(
            evidence.alternate_homopolymer[alternate_index]->run_length
        );
    }
    context.filter_passed = filter_decision.passed;
    context.variant_type = variant.alternates[alternate_index].type;
    return context;
}

VariantPrioritizationResult evaluate_variant_prioritization(
    const VariantPrioritizationContext& context,
    const VariantPrioritizationPolicy& policy
) {
    validate_variant_prioritization_policy(policy);
    VariantPrioritizationResult result;
    result.score = policy.baseline_score;
    result.trace.reserve(policy.rules.size());

    for (const auto& rule : policy.rules) {
        VariantPrioritizationTrace trace;
        trace.rule_id = rule.id;
        trace.observed = feature_value(context, rule.feature);
        if (!trace.observed.has_value()) {
            trace.status = PrioritizationRuleStatus::missing;
            if (rule.missing_behavior == MissingFeatureBehavior::reject_evaluation) {
                throw std::invalid_argument("prioritization rule '" + rule.id + "' requires missing feature '" +
                    std::string{prioritization_feature_name(rule.feature)} + "'");
            }
            result.trace.push_back(std::move(trace));
            continue;
        }

        if (rule_matches(*trace.observed, rule)) {
            trace.status = PrioritizationRuleStatus::matched;
            trace.applied_score_delta = rule.score_delta;
            const double updated = result.score + rule.score_delta;
            if (!std::isfinite(updated)) throw std::overflow_error("prioritization score became non-finite");
            result.score = updated;
        } else {
            trace.status = PrioritizationRuleStatus::not_matched;
        }
        result.trace.push_back(std::move(trace));
    }
    return result;
}

VariantPriorityKey make_variant_priority_key(
    const VariantRecord& variant,
    const std::size_t alternate_index
) {
    if (const auto variant_error = validate_variant_record(variant); variant_error.has_value()) {
        throw std::invalid_argument(*variant_error);
    }
    if (alternate_index >= variant.alternates.size()) {
        throw std::out_of_range("priority key alternate index exceeds ALT cardinality");
    }
    return VariantPriorityKey{
        variant.locus.contig_id,
        variant.locus.start,
        variant.locus.end,
        variant.reference.sequence,
        variant.alternates[alternate_index].sequence,
        alternate_index
    };
}

std::string_view prioritization_feature_name(const PrioritizationFeature feature) noexcept {
    switch (feature) {
        case PrioritizationFeature::depth: return "DP";
        case PrioritizationFeature::variant_allele_fraction: return "VAF";
        case PrioritizationFeature::genotype_quality: return "GQ";
        case PrioritizationFeature::mapping_quality: return "MQ";
        case PrioritizationFeature::alternate_strand_fraction: return "ALT_STRAND_FRACTION";
        case PrioritizationFeature::homopolymer_run_length: return "HOMOPOLYMER_RUN";
        case PrioritizationFeature::filter_passed: return "FILTER_PASSED";
        case PrioritizationFeature::variant_type: return "VARIANT_TYPE";
    }
    return "UNKNOWN_FEATURE";
}

std::string_view prioritization_rule_status_name(const PrioritizationRuleStatus status) noexcept {
    switch (status) {
        case PrioritizationRuleStatus::matched: return "MATCHED";
        case PrioritizationRuleStatus::not_matched: return "NOT_MATCHED";
        case PrioritizationRuleStatus::missing: return "MISSING";
    }
    return "UNKNOWN_STATUS";
}

}  // namespace biocore::domain
