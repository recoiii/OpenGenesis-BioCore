#include "biocore/domain/probabilistic_genotyping.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using namespace biocore::domain;

[[nodiscard]] bool require(const bool condition, const char* message) {
    if (!condition) std::cerr << "probabilistic_genotyping_prior_tests: " << message << '\n';
    return condition;
}

[[nodiscard]] VariantRecord triallelic_variant() {
    VariantRecord record;
    record.locus = GenomicInterval{0U, 100U, 101U};
    record.reference = Allele{"A", VariantType::unknown, false};
    record.alternates.push_back(Allele{"C", VariantType::snv, false});
    record.alternates.push_back(Allele{"G", VariantType::snv, false});
    return record;
}

}  // namespace

int main() {
    using namespace biocore::domain;
    bool ok = true;

    const VariantRecord variant = triallelic_variant();
    const auto evidence = make_count_genotype_evidence({1U, 1U, 0U}, 6U, 0.05);

    ProbabilisticGenotypingOptions uniform;
    const auto uniform_call = genotype_variant(variant, evidence, uniform);
    ok = require(uniform_call.call.phred_likelihoods.size() == 6U, "triallelic diploid PL is Number=G") && ok;

    ProbabilisticGenotypingOptions prior_shifted;
    prior_shifted.genotype_priors = {1000.0, 1.0, 1.0, 1.0, 1.0, 1.0};
    const auto shifted = genotype_variant(variant, evidence, prior_shifted);
    ok = require(shifted.best_genotype_index == 0U, "strong supplied prior can shift weak evidence to 0/0") && ok;
    ok = require(shifted.call.allele_indices[0U] == 0 && shifted.call.allele_indices[1U] == 0,
                 "posterior-selected genotype reflects supplied prior") && ok;
    ok = require(shifted.log10_likelihoods == uniform_call.log10_likelihoods,
                 "priors change posterior but not raw genotype likelihoods/PL basis") && ok;

    ProbabilisticGenotypingOptions triploid;
    triploid.ploidy = 3U;
    const auto triploid_call = genotype_variant(
        variant,
        make_count_genotype_evidence({3U, 3U, 3U}, 0U, 0.02),
        triploid
    );
    ok = require(triploid_call.call.ploidy() == 3U, "triploid probabilistic genotype is supported") && ok;
    ok = require(triploid_call.call.phred_likelihoods.size() == 10U, "triploid triallelic PL is Number=G") && ok;
    ok = require(!validate_genotype_call(triploid_call.call, 3U).has_value(), "triploid output validates") && ok;

    bool bad_prior_rejected = false;
    try {
        ProbabilisticGenotypingOptions bad;
        bad.genotype_priors = {1.0, 1.0};
        static_cast<void>(genotype_variant(variant, evidence, bad));
    } catch (const std::invalid_argument&) {
        bad_prior_rejected = true;
    }
    ok = require(bad_prior_rejected, "wrong prior Number=G cardinality is rejected") && ok;

    bool safety_bound_rejected = false;
    try {
        ProbabilisticGenotypingOptions bounded;
        bounded.ploidy = 6U;
        bounded.maximum_genotype_count = 10U;
        static_cast<void>(genotype_variant(variant, evidence, bounded));
    } catch (const std::invalid_argument&) {
        safety_bound_rejected = true;
    }
    ok = require(safety_bound_rejected, "genotype-state explosion is safety bounded") && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
