#include "biocore/domain/variant_filtering.hpp"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using namespace biocore::domain;

[[nodiscard]] bool require(const bool condition, const char* message) {
    if (!condition) std::cerr << "advanced_variant_filtering_tests: " << message << '\n';
    return condition;
}

[[nodiscard]] VariantRecord biallelic_snv() {
    VariantRecord variant;
    variant.locus = GenomicInterval{0U, 10U, 11U};
    variant.reference = Allele{"A", VariantType::unknown, false};
    variant.alternates.push_back(Allele{"G", VariantType::snv, false});
    variant.info.mapping_quality = 55.0F;
    return variant;
}

[[nodiscard]] VariantRecord triallelic_snv() {
    VariantRecord variant;
    variant.locus = GenomicInterval{0U, 20U, 21U};
    variant.reference = Allele{"A", VariantType::unknown, false};
    variant.alternates.push_back(Allele{"C", VariantType::snv, false});
    variant.alternates.push_back(Allele{"G", VariantType::snv, false});
    variant.info.mapping_quality = 60.0F;
    return variant;
}

[[nodiscard]] bool has_reason(const VariantFilterDecision& decision, const VariantFilterReason reason) {
    for (const auto observed : decision.reasons) {
        if (observed == reason) return true;
    }
    return false;
}

}  // namespace

int main() {
    using namespace biocore::domain;
    bool ok = true;

    const VariantRecord variant = biallelic_snv();
    const auto genotype = genotype_variant(variant, make_count_genotype_evidence({12U, 8U}, 2U, 0.01));
    const VariantFilterEvidence evidence = make_variant_filter_evidence(variant, genotype);

    AdvancedVariantFilterPolicy passing;
    passing.minimum_depth = 20U;
    passing.minimum_variant_allele_fraction = 0.30;
    passing.minimum_genotype_quality = 20U;
    passing.minimum_mapping_quality = 40.0;
    const auto pass = evaluate_variant_filter(variant, evidence, 0U, passing);
    ok = require(pass.passed, "well-supported SNV did not pass basic DP/VAF/GQ/MQ policy") && ok;
    ok = require(pass.variant_allele_fraction.has_value() && *pass.variant_allele_fraction > 0.36 &&
                 *pass.variant_allele_fraction < 0.37,
                 "VAF must use ALT AD divided by DP including ambiguous observations") && ok;

    AdvancedVariantFilterPolicy strict = passing;
    strict.minimum_depth = 30U;
    strict.minimum_variant_allele_fraction = 0.50;
    strict.minimum_genotype_quality = 100U;
    strict.minimum_mapping_quality = 70.0;
    const auto failed = evaluate_variant_filter(variant, evidence, 0U, strict);
    ok = require(!failed.passed, "strict quality policy unexpectedly passed") && ok;
    ok = require(has_reason(failed, VariantFilterReason::low_depth), "LOW_DP reason missing") && ok;
    ok = require(has_reason(failed, VariantFilterReason::low_variant_allele_fraction), "LOW_VAF reason missing") && ok;
    ok = require(has_reason(failed, VariantFilterReason::low_genotype_quality), "LOW_GQ reason missing") && ok;
    ok = require(has_reason(failed, VariantFilterReason::low_mapping_quality), "LOW_MQ reason missing") && ok;

    VariantFilterEvidence missing;
    const auto missing_result = evaluate_variant_filter(variant, missing, 0U, passing);
    ok = require(has_reason(missing_result, VariantFilterReason::missing_depth), "requested missing DP must fail closed") && ok;
    ok = require(has_reason(missing_result, VariantFilterReason::missing_variant_allele_fraction), "requested missing VAF must fail closed") && ok;
    ok = require(has_reason(missing_result, VariantFilterReason::missing_genotype_quality), "requested missing GQ must fail closed") && ok;
    ok = require(has_reason(missing_result, VariantFilterReason::missing_mapping_quality), "requested missing MQ must fail closed") && ok;

    const VariantRecord multi = triallelic_snv();
    const auto multi_call = genotype_variant(multi, make_count_genotype_evidence({10U, 2U, 8U}, 0U, 0.01));
    const auto multi_evidence = make_variant_filter_evidence(multi, multi_call);
    AdvancedVariantFilterPolicy vaf_only;
    vaf_only.minimum_variant_allele_fraction = 0.20;
    const auto alt1 = evaluate_variant_filter(multi, multi_evidence, 0U, vaf_only);
    const auto alt2 = evaluate_variant_filter(multi, multi_evidence, 1U, vaf_only);
    ok = require(!alt1.passed && has_reason(alt1, VariantFilterReason::low_variant_allele_fraction),
                 "multi-allelic ALT1 VAF must be evaluated independently") && ok;
    ok = require(alt2.passed, "multi-allelic ALT2 VAF should pass independently") && ok;

    bool bad_policy_rejected = false;
    try {
        AdvancedVariantFilterPolicy bad;
        bad.minimum_variant_allele_fraction = 1.1;
        validate_advanced_variant_filter_policy(bad);
    } catch (const std::invalid_argument&) {
        bad_policy_rejected = true;
    }
    ok = require(bad_policy_rejected, "invalid VAF policy was accepted") && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
