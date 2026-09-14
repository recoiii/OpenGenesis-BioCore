#include "biocore/domain/genotype.hpp"

#include <cassert>
#include <cstddef>

int main() {
    using biocore::domain::GenotypeCall;
    using biocore::domain::genotype_likelihood_count;
    using biocore::domain::missing_allele_index;
    using biocore::domain::normalize_phred_likelihoods;
    using biocore::domain::validate_genotype_call;

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
    assert(diploid.phred_likelihoods[0] == 30U);
    assert(diploid.phred_likelihoods[1] == 0U);
    assert(diploid.phred_likelihoods[2] == 60U);
    assert(!diploid.allele_indices.uses_heap_storage());
    assert(!diploid.allele_depths.uses_heap_storage());
    assert(!diploid.phred_likelihoods.uses_heap_storage());
    assert(!validate_genotype_call(diploid, 2U).has_value());

    GenotypeCall haploid;
    haploid.allele_indices.push_back(1);
    haploid.allele_depths.push_back(2U);
    haploid.allele_depths.push_back(8U);
    haploid.phred_likelihoods.push_back(50U);
    haploid.phred_likelihoods.push_back(0U);
    assert(!validate_genotype_call(haploid, 2U).has_value());

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
    assert(expected.has_value() && *expected == 10U);
    for (std::size_t index = 0U; index < *expected; ++index) {
        triploid.phred_likelihoods.push_back(index == 3U ? 0U : 50U);
    }
    assert(triploid.allele_indices.uses_heap_storage());
    assert(triploid.phred_likelihoods.uses_heap_storage());
    assert(!validate_genotype_call(triploid, 3U).has_value());

    GenotypeCall missing;
    missing.allele_indices.push_back(missing_allele_index);
    missing.allele_indices.push_back(missing_allele_index);
    missing.phased_separators.push_back(0U);
    assert(missing.fully_missing());
    assert(!missing.partially_missing());

    GenotypeCall partial;
    partial.allele_indices.push_back(0);
    partial.allele_indices.push_back(missing_allele_index);
    partial.phased_separators.push_back(0U);
    assert(!partial.fully_missing());
    assert(partial.partially_missing());
    return 0;
}
