#include "biocore/domain/variant_prioritization.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
using namespace biocore::domain;

[[nodiscard]] bool require(const bool condition, const char* message) {
    if (!condition) std::cerr << "variant_prioritization_tests: " << message << '\n';
    return condition;
}

[[nodiscard]] VariantRecord make_variant() {
    VariantRecord variant;
    variant.locus = GenomicInterval{2U, 100U, 101U};
    variant.reference = Allele{"A", VariantType::unknown, false};
    variant.alternates.push_back(Allele{"G", VariantType::snv, false});
    variant.info.mapping_quality = 58.0F;
    return variant;
}

}  // namespace

int main() {
    using namespace biocore::domain;
    bool ok = true;

    const VariantRecord variant = make_variant();
    const auto genotype = genotype_variant(variant, make_count_genotype_evidence({12U, 8U}, 0U, 0.01));
    const auto evidence = make_variant_filter_evidence(variant, genotype);
    AdvancedVariantFilterPolicy filter_policy;
    filter_policy.minimum_depth = 10U;
    filter_policy.minimum_variant_allele_fraction = 0.30;
    const auto decision = evaluate_variant_filter(variant, evidence, 0U, filter_policy);
    const auto context = make_variant_prioritization_context(variant, evidence, decision);

    VariantPrioritizationPolicy policy;
    policy.baseline_score = 1.0;
    policy.rules = {
        {"pass-filter", PrioritizationFeature::filter_passed, PrioritizationOperator::equal, true, 2.0, MissingFeatureBehavior::skip_rule},
        {"vaf-strong", PrioritizationFeature::variant_allele_fraction, PrioritizationOperator::greater_or_equal, 0.35, 3.0, MissingFeatureBehavior::reject_evaluation},
        {"mq-high", PrioritizationFeature::mapping_quality, PrioritizationOperator::greater_or_equal, 50.0, 1.5, MissingFeatureBehavior::skip_rule},
        {"snv", PrioritizationFeature::variant_type, PrioritizationOperator::equal, VariantType::snv, 0.5, MissingFeatureBehavior::skip_rule},
        {"deep", PrioritizationFeature::depth, PrioritizationOperator::greater_than, 100.0, 10.0, MissingFeatureBehavior::skip_rule}
    };
    const auto result = evaluate_variant_prioritization(context, policy);
    ok = require(result.score == 8.0, "unexpected cumulative prioritization score") && ok;
    ok = require(result.trace.size() == policy.rules.size(), "trace must preserve one entry per rule") && ok;
    ok = require(result.trace[0].status == PrioritizationRuleStatus::matched, "filter-pass rule should match") && ok;
    ok = require(result.trace[1].status == PrioritizationRuleStatus::matched, "VAF rule should match") && ok;
    ok = require(result.trace[4].status == PrioritizationRuleStatus::not_matched, "depth rule should not match") && ok;
    ok = require(context.variant_type == VariantType::snv, "context must retain ALT variant type") && ok;

    VariantPrioritizationContext missing = context;
    missing.mapping_quality.reset();
    VariantPrioritizationPolicy skip_policy;
    skip_policy.rules = {{"missing-mq", PrioritizationFeature::mapping_quality, PrioritizationOperator::greater_or_equal, 50.0, 7.0, MissingFeatureBehavior::skip_rule}};
    const auto skipped = evaluate_variant_prioritization(missing, skip_policy);
    ok = require(skipped.score == 0.0 && skipped.trace[0].status == PrioritizationRuleStatus::missing,
                 "skip_rule must preserve missing trace without applying score") && ok;

    bool rejected_missing = false;
    try {
        skip_policy.rules[0].missing_behavior = MissingFeatureBehavior::reject_evaluation;
        static_cast<void>(evaluate_variant_prioritization(missing, skip_policy));
    } catch (const std::invalid_argument&) {
        rejected_missing = true;
    }
    ok = require(rejected_missing, "reject_evaluation must fail on missing requested feature") && ok;

    bool duplicate_rejected = false;
    try {
        VariantPrioritizationPolicy bad;
        bad.rules = {
            {"same", PrioritizationFeature::depth, PrioritizationOperator::greater_than, 1.0, 1.0, MissingFeatureBehavior::skip_rule},
            {"same", PrioritizationFeature::depth, PrioritizationOperator::greater_than, 2.0, 1.0, MissingFeatureBehavior::skip_rule}
        };
        validate_variant_prioritization_policy(bad);
    } catch (const std::invalid_argument&) {
        duplicate_rejected = true;
    }
    ok = require(duplicate_rejected, "duplicate prioritization rule ids must be rejected") && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
