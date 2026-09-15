#include "biocore/domain/variant_prioritization.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>

int main() {
    using namespace biocore::domain;

    VariantPrioritizationContext context;
    context.alternate_index = 0U;
    context.depth = 40.0;
    context.variant_allele_fraction = 0.5;
    context.genotype_quality = 60.0;
    context.mapping_quality = 55.0;
    context.alternate_strand_fraction = 0.4;
    context.homopolymer_run_length = 3.0;
    context.filter_passed = true;
    context.variant_type = VariantType::snv;

    VariantPrioritizationPolicy policy;
    policy.baseline_score = 1.0;
    policy.rules = {
        {"pass", PrioritizationFeature::filter_passed, PrioritizationOperator::equal, true, 1.0, MissingFeatureBehavior::reject_evaluation},
        {"vaf", PrioritizationFeature::variant_allele_fraction, PrioritizationOperator::greater_or_equal, 0.2, 2.0, MissingFeatureBehavior::reject_evaluation},
        {"gq", PrioritizationFeature::genotype_quality, PrioritizationOperator::greater_or_equal, 20.0, 3.0, MissingFeatureBehavior::reject_evaluation},
        {"mq", PrioritizationFeature::mapping_quality, PrioritizationOperator::greater_or_equal, 30.0, 4.0, MissingFeatureBehavior::reject_evaluation},
        {"snv", PrioritizationFeature::variant_type, PrioritizationOperator::equal, VariantType::snv, 5.0, MissingFeatureBehavior::skip_rule}
    };

    constexpr std::uint64_t evaluations = 1'000'000U;
    std::uint64_t matched_rules = 0U;
    std::uint64_t score_checksum = 0U;
    const auto started = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0U; index < evaluations; ++index) {
        const auto result = evaluate_variant_prioritization(context, policy);
        score_checksum += static_cast<std::uint64_t>(result.score * 100.0);
        for (const auto& trace : result.trace) {
            if (trace.status == PrioritizationRuleStatus::matched) ++matched_rules;
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started
    ).count();

    std::cout << "evaluations=" << evaluations << '\n';
    std::cout << "matched_rules=" << matched_rules << '\n';
    std::cout << "score_checksum=" << score_checksum << '\n';
    std::cout << "elapsed_ms=" << elapsed << '\n';

    return evaluations == 1'000'000U && matched_rules == 5'000'000U &&
        score_checksum == 1'600'000'000U ? EXIT_SUCCESS : EXIT_FAILURE;
}
