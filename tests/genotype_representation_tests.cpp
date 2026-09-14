#include "biocore/domain/genotype.hpp"

#include <cstddef>
#include <cstdlib>
#include <iostream>

namespace {

[[nodiscard]] bool require(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "genotype_representation_tests: " << message << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main() {
    using biocore::domain::GenotypeCall;
    using biocore::domain::genotype_likelihood_count;
    using biocore::domain::missing_allele_index;
    using biocore::domain::normalize_phred_likelihoods;
    using biocore::domain::validate_genotype_call;

    bool ok = true;

    GenotypeCall diploid;
    diploid.allele_indices.push_back(0);
    diploid.allele_indices.push_back(1);
    diploid.phased_separators.push_back(0U);
    diploid.depth = 30U;
    diploid.allele_depths.push_back(18U);
    diploid.allele_depths.push_back(12U);
    diploid.genotype_quality = 99U;
    diploid.phred_likelihoods.push_back(40U);
    diploid.phred_likelihoods.push_back(10U);
    diploid.phred_likelihoods.push_back(70U);
    normalize_phred_likelihoods(diploid);
    ok = require(diploid.phred_likelihoods[0] == 30U, "PL normalization first value") && ok;
    ok = require(diploid.phred_likelihoods[1] == 0U, "PL normalization minimum zero") && ok;
    ok = require(diploid.phred_likelihoods[2] == 60U, "PL normalization third value") && ok;
    ok = require(!diploid.allele_indices.uses_heap_storage(), "diploid GT stays inline") && ok;
    ok = require(!diploid.allele_depths.uses_heap_storage(), "biallelic AD stays inline") && ok;
    ok = require(!diploid.phred_likelihoods.uses_heap_storage(), "biallelic diploid PL stays inline") && ok;
    ok = require(!validate_genotype_call(diploid, 2U).has_value(), "valid diploid genotype") && ok;

    GenotypeCall haploid;
    haploid.allele_indices.push_back(1);
    haploid.allele_depths.push_back(2U);
    haploid.allele_depths.push_back(8U);
    haploid.phred_likelihoods.push_back(50U);
    haploid.phred_likelihoods.push_back(0U);
    ok = require(!validate_genotype_call(haploid, 2U).has_value(), "valid haploid genotype") && ok;

    GenotypeCall triploid;
    triploid.allele_indices.push_back(0);
    triploid.allele_indices.push_back(1);
    triploid.allele_indices.push_back(2);
    triploid.phased_separators.push_back(1U);
    triploid.phased_separators.push_back(0U);
    triploid.allele_depths.push_back(10U);
    triploid.allele_depths.push_back(10U);
    triploid.allele_depths.push_back(10U);
    const auto expected = genotype_likelihood_count(3U, 3U);
    ok = require(expected.has_value() && *expected == 10U, "triploid Number=G cardinality") && ok;
    if (expected.has_value()) {
        for (std::size_t index = 0U; index < *expected; ++index) {
            triploid.phred_likelihoods.push_back(index == 3U ? 0U : 50U);
        }
    }
    ok = require(triploid.allele_indices.uses_heap_storage(), "triploid GT spills safely") && ok;
    ok = require(triploid.phred_likelihoods.uses_heap_storage(), "triploid multiallelic PL spills safely") && ok;
    ok = require(!validate_genotype_call(triploid, 3U).has_value(), "valid triploid genotype") && ok;

    GenotypeCall missing;
    missing.allele_indices.push_back(missing_allele_index);
    missing.allele_indices.push_back(missing_allele_index);
    missing.phased_separators.push_back(0U);
    ok = require(missing.fully_missing(), "fully missing genotype") && ok;
    ok = require(!missing.partially_missing(), "fully missing not partial") && ok;

    GenotypeCall partial;
    partial.allele_indices.push_back(0);
    partial.allele_indices.push_back(missing_allele_index);
    partial.phased_separators.push_back(0U);
    ok = require(!partial.fully_missing(), "partial genotype not fully missing") && ok;
    ok = require(partial.partially_missing(), "partially missing genotype") && ok;

    GenotypeCall bad_ad = diploid;
    bad_ad.allele_depths.clear();
    bad_ad.allele_depths.push_back(30U);
    ok = require(validate_genotype_call(bad_ad, 2U).has_value(), "AD Number=R mismatch rejected") && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
