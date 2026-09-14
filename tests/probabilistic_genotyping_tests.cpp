#include "biocore/domain/probabilistic_genotyping.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace {
using namespace biocore::domain;

[[nodiscard]] bool require(const bool condition, const char* message) {
    if (!condition) std::cerr << "probabilistic_genotyping_tests: " << message << '\n';
    return condition;
}

[[nodiscard]] VariantRecord biallelic_snv() {
    VariantRecord record;
    record.locus = GenomicInterval{0U, 10U, 11U};
    record.reference = Allele{"A", VariantType::unknown, false};
    record.alternates.push_back(Allele{"G", VariantType::snv, false});
    return record;
}

}  // namespace

int main() {
    using namespace biocore::domain;
    bool ok = true;

    const auto diploid = enumerate_vcf_genotypes(2U, 2U);
    ok = require(diploid.size() == 3U, "diploid biallelic Number=G count") && ok;
    ok = require(diploid[0U] == std::vector<std::int16_t>({0, 0}), "VCF order 00") && ok;
    ok = require(diploid[1U] == std::vector<std::int16_t>({0, 1}), "VCF order 01") && ok;
    ok = require(diploid[2U] == std::vector<std::int16_t>({1, 1}), "VCF order 11") && ok;

    const auto triploid = enumerate_vcf_genotypes(3U, 3U);
    const std::vector<std::vector<std::int16_t>> expected_triploid{
        {0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {1, 1, 1}, {0, 0, 2},
        {0, 1, 2}, {1, 1, 2}, {0, 2, 2}, {1, 2, 2}, {2, 2, 2}
    };
    ok = require(triploid == expected_triploid, "VCF triploid multiallelic genotype ordering") && ok;

    const VariantRecord snv = biallelic_snv();
    const auto heterozygous_evidence = make_count_genotype_evidence({18U, 17U}, 1U, 0.01);
    const auto heterozygous = genotype_variant(snv, heterozygous_evidence);
    ok = require(heterozygous.call.allele_indices.size() == 2U, "diploid genotype emitted") && ok;
    ok = require(heterozygous.call.allele_indices[0U] == 0 && heterozygous.call.allele_indices[1U] == 1,
                 "balanced SNV evidence calls heterozygous genotype") && ok;
    ok = require(heterozygous.call.depth == 36U, "DP includes ambiguous observations") && ok;
    ok = require(heterozygous.call.allele_depths.size() == 2U &&
                 heterozygous.call.allele_depths[0U] == 18U && heterozygous.call.allele_depths[1U] == 17U,
                 "AD preserves assigned REF/ALT observations") && ok;
    ok = require(heterozygous.call.phred_likelihoods.size() == 3U &&
                 heterozygous.call.phred_likelihoods[1U] == 0U,
                 "PL values use VCF canonical order and best likelihood is zero") && ok;
    ok = require(heterozygous.call.genotype_quality.has_value() && *heterozygous.call.genotype_quality > 20U,
                 "strong balanced evidence yields non-trivial GQ") && ok;
    ok = require(std::abs(std::accumulate(
                     heterozygous.posterior_probabilities.begin(),
                     heterozygous.posterior_probabilities.end(),
                     0.0) - 1.0) < 1e-12,
                 "posterior probabilities normalize to one") && ok;

    const auto hom_ref = genotype_variant(snv, make_count_genotype_evidence({40U, 0U}, 0U, 0.01));
    ok = require(hom_ref.call.allele_indices[0U] == 0 && hom_ref.call.allele_indices[1U] == 0,
                 "reference-only evidence calls hom-ref") && ok;

    const auto hom_alt = genotype_variant(snv, make_count_genotype_evidence({0U, 40U}, 0U, 0.01));
    ok = require(hom_alt.call.allele_indices[0U] == 1 && hom_alt.call.allele_indices[1U] == 1,
                 "alternate-only evidence calls hom-alt") && ok;

    bool empty_rejected = false;
    try {
        static_cast<void>(make_count_genotype_evidence({0U, 0U}, 0U, 0.01));
    } catch (const std::invalid_argument&) {
        empty_rejected = true;
    }
    ok = require(empty_rejected, "zero-observation evidence is rejected") && ok;

    bool mismatch_rejected = false;
    try {
        static_cast<void>(genotype_variant(snv, make_count_genotype_evidence({10U, 5U, 1U}, 0U, 0.01)));
    } catch (const std::invalid_argument&) {
        mismatch_rejected = true;
    }
    ok = require(mismatch_rejected, "VariantRecord/evidence allele-count mismatch is rejected") && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
