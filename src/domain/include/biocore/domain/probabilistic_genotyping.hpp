#pragma once

#include "biocore/domain/genotype.hpp"
#include "biocore/domain/indel_candidate.hpp"
#include "biocore/domain/variant_record.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace biocore::domain {

struct GenotypeEvidenceBucket final {
    std::uint32_t observations{0U};
    std::vector<double> allele_likelihoods;
};

struct GenotypeEvidence final {
    std::size_t allele_count{0U};
    std::vector<GenotypeEvidenceBucket> buckets;
    std::vector<std::uint32_t> allele_depths;
};

struct ProbabilisticGenotypingOptions final {
    std::size_t ploidy{2U};
    std::size_t maximum_genotype_count{4096U};
    std::uint32_t maximum_phred_likelihood{999U};
    std::uint32_t maximum_genotype_quality{99U};
    std::vector<double> genotype_priors;
};

struct ProbabilisticGenotypeResult final {
    GenotypeCall call;
    std::size_t best_genotype_index{0U};
    double best_posterior_probability{0.0};
    std::vector<double> log10_likelihoods;
    std::vector<double> posterior_probabilities;
};

[[nodiscard]] std::optional<std::string> validate_genotype_evidence(const GenotypeEvidence& evidence);
void validate_probabilistic_genotyping_options(
    const ProbabilisticGenotypingOptions& options,
    std::size_t allele_count
);

[[nodiscard]] std::vector<std::vector<std::int16_t>> enumerate_vcf_genotypes(
    std::size_t allele_count,
    std::size_t ploidy,
    std::size_t maximum_genotype_count = 4096U
);

[[nodiscard]] GenotypeEvidence make_count_genotype_evidence(
    const std::vector<std::uint32_t>& allele_counts,
    std::uint32_t ambiguous_observations,
    double error_probability
);

[[nodiscard]] ProbabilisticGenotypeResult call_probabilistic_genotype(
    const GenotypeEvidence& evidence,
    const ProbabilisticGenotypingOptions& options = {}
);

[[nodiscard]] ProbabilisticGenotypeResult genotype_variant(
    const VariantRecord& variant,
    const GenotypeEvidence& evidence,
    const ProbabilisticGenotypingOptions& options = {}
);

[[nodiscard]] ProbabilisticGenotypeResult genotype_indel_candidate(
    const IndelCandidate& candidate,
    const ProbabilisticGenotypingOptions& options = {},
    double realignment_error_probability = 0.01
);

}  // namespace biocore::domain
