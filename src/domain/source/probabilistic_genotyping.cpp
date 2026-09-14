#include "biocore/domain/probabilistic_genotyping.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace biocore::domain {
namespace {

[[nodiscard]] bool finite_nonnegative(const double value) noexcept {
    return std::isfinite(value) && value >= 0.0;
}

[[nodiscard]] std::uint32_t checked_observation_count(const GenotypeEvidence& evidence) {
    std::uint64_t total = 0U;
    for (const auto& bucket : evidence.buckets) {
        total += bucket.observations;
        if (total > std::numeric_limits<std::uint32_t>::max()) {
            throw std::invalid_argument("genotype evidence depth exceeds uint32 range");
        }
    }
    return static_cast<std::uint32_t>(total);
}

void enumerate_recursive(
    const std::size_t remaining,
    const std::int16_t maximum_allele,
    std::vector<std::int16_t>& descending_suffix,
    std::vector<std::vector<std::int16_t>>& output,
    const std::size_t maximum_genotype_count
) {
    if (remaining == 0U) {
        if (output.size() >= maximum_genotype_count) {
            throw std::invalid_argument("genotype enumeration exceeds configured safety bound");
        }
        std::vector<std::int16_t> genotype(descending_suffix.rbegin(), descending_suffix.rend());
        output.push_back(std::move(genotype));
        return;
    }

    for (std::int32_t allele = 0; allele <= maximum_allele; ++allele) {
        descending_suffix.push_back(static_cast<std::int16_t>(allele));
        enumerate_recursive(
            remaining - 1U,
            static_cast<std::int16_t>(allele),
            descending_suffix,
            output,
            maximum_genotype_count
        );
        descending_suffix.pop_back();
    }
}

[[nodiscard]] std::vector<double> normalized_priors(
    const ProbabilisticGenotypingOptions& options,
    const std::size_t genotype_count
) {
    if (options.genotype_priors.empty()) {
        return std::vector<double>(genotype_count, 1.0 / static_cast<double>(genotype_count));
    }
    if (options.genotype_priors.size() != genotype_count) {
        throw std::invalid_argument("genotype prior cardinality must equal Number=G");
    }
    double total = 0.0;
    for (const double prior : options.genotype_priors) {
        if (!finite_nonnegative(prior)) {
            throw std::invalid_argument("genotype priors must be finite and non-negative");
        }
        total += prior;
    }
    if (!(total > 0.0) || !std::isfinite(total)) {
        throw std::invalid_argument("genotype priors must contain positive finite mass");
    }
    std::vector<double> result;
    result.reserve(genotype_count);
    for (const double prior : options.genotype_priors) {
        result.push_back(prior / total);
    }
    return result;
}

[[nodiscard]] double genotype_bucket_probability(
    const std::vector<std::int16_t>& genotype,
    const GenotypeEvidenceBucket& bucket
) {
    double probability = 0.0;
    for (const std::int16_t allele : genotype) {
        probability += bucket.allele_likelihoods[static_cast<std::size_t>(allele)];
    }
    return probability / static_cast<double>(genotype.size());
}

[[nodiscard]] std::uint32_t phred_from_probability_error(
    const double error_probability,
    const std::uint32_t maximum
) {
    if (error_probability <= 0.0) {
        return maximum;
    }
    if (error_probability >= 1.0) {
        return 0U;
    }
    const double raw = -10.0 * std::log10(error_probability);
    if (!std::isfinite(raw) || raw >= static_cast<double>(maximum)) {
        return maximum;
    }
    if (raw <= 0.0) {
        return 0U;
    }
    return static_cast<std::uint32_t>(std::llround(raw));
}

[[nodiscard]] std::uint32_t phred_likelihood_delta(
    const double best_log10_likelihood,
    const double log10_likelihood,
    const std::uint32_t maximum
) {
    if (!std::isfinite(log10_likelihood)) {
        return maximum;
    }
    double delta = 10.0 * (best_log10_likelihood - log10_likelihood);
    if (delta < 0.0 && delta > -1e-9) {
        delta = 0.0;
    }
    if (delta <= 0.0) {
        return 0U;
    }
    if (!std::isfinite(delta) || delta >= static_cast<double>(maximum)) {
        return maximum;
    }
    return static_cast<std::uint32_t>(std::llround(delta));
}

}  // namespace

std::optional<std::string> validate_genotype_evidence(const GenotypeEvidence& evidence) {
    if (evidence.allele_count == 0U) {
        return "genotype evidence must contain at least one allele";
    }
    if (evidence.allele_count > static_cast<std::size_t>(std::numeric_limits<std::int16_t>::max()) + 1U) {
        return "genotype evidence allele count exceeds GenotypeCall allele-index capacity";
    }
    if (evidence.buckets.empty()) {
        return "genotype evidence must contain at least one observation bucket";
    }
    std::uint64_t total_observations = 0U;
    for (const auto& bucket : evidence.buckets) {
        if (bucket.observations == 0U) {
            return "genotype evidence bucket observation count must be positive";
        }
        if (bucket.allele_likelihoods.size() != evidence.allele_count) {
            return "genotype evidence likelihood cardinality must equal allele_count";
        }
        bool positive = false;
        for (const double likelihood : bucket.allele_likelihoods) {
            if (!finite_nonnegative(likelihood) || likelihood > 1.0) {
                return "genotype evidence likelihoods must be finite probabilities in [0,1]";
            }
            positive = positive || likelihood > 0.0;
        }
        if (!positive) {
            return "genotype evidence bucket must assign positive likelihood to at least one allele";
        }
        total_observations += bucket.observations;
        if (total_observations > std::numeric_limits<std::uint32_t>::max()) {
            return "genotype evidence depth exceeds uint32 range";
        }
    }
    if (!evidence.allele_depths.empty()) {
        if (evidence.allele_depths.size() != evidence.allele_count) {
            return "genotype evidence AD cardinality must be Number=R";
        }
        std::uint64_t assigned = 0U;
        for (const std::uint32_t count : evidence.allele_depths) {
            assigned += count;
        }
        if (assigned > total_observations) {
            return "genotype evidence assigned allele depths exceed total observations";
        }
    }
    return std::nullopt;
}

void validate_probabilistic_genotyping_options(
    const ProbabilisticGenotypingOptions& options,
    const std::size_t allele_count
) {
    if (options.ploidy == 0U) {
        throw std::invalid_argument("genotyping ploidy must be positive");
    }
    if (options.maximum_genotype_count == 0U) {
        throw std::invalid_argument("maximum_genotype_count must be positive");
    }
    if (options.maximum_phred_likelihood == 0U) {
        throw std::invalid_argument("maximum_phred_likelihood must be positive");
    }
    if (options.maximum_genotype_quality == 0U) {
        throw std::invalid_argument("maximum_genotype_quality must be positive");
    }
    const auto count = genotype_likelihood_count(allele_count, options.ploidy);
    if (!count.has_value()) {
        throw std::invalid_argument("genotype Number=G cardinality overflows");
    }
    if (*count > options.maximum_genotype_count) {
        throw std::invalid_argument("genotype Number=G cardinality exceeds configured safety bound");
    }
    if (!options.genotype_priors.empty() && options.genotype_priors.size() != *count) {
        throw std::invalid_argument("genotype prior cardinality must equal Number=G");
    }
}

std::vector<std::vector<std::int16_t>> enumerate_vcf_genotypes(
    const std::size_t allele_count,
    const std::size_t ploidy,
    const std::size_t maximum_genotype_count
) {
    if (allele_count == 0U || ploidy == 0U || maximum_genotype_count == 0U) {
        throw std::invalid_argument("allele_count, ploidy and maximum_genotype_count must be positive");
    }
    if (allele_count > static_cast<std::size_t>(std::numeric_limits<std::int16_t>::max()) + 1U) {
        throw std::invalid_argument("allele_count exceeds GenotypeCall allele-index capacity");
    }
    const auto expected = genotype_likelihood_count(allele_count, ploidy);
    if (!expected.has_value() || *expected > maximum_genotype_count) {
        throw std::invalid_argument("genotype enumeration exceeds configured safety bound");
    }

    std::vector<std::vector<std::int16_t>> output;
    output.reserve(*expected);
    std::vector<std::int16_t> suffix;
    suffix.reserve(ploidy);
    const auto maximum_allele = static_cast<std::int16_t>(allele_count - 1U);
    for (std::int32_t allele = 0; allele <= maximum_allele; ++allele) {
        suffix.push_back(static_cast<std::int16_t>(allele));
        enumerate_recursive(ploidy - 1U, static_cast<std::int16_t>(allele), suffix, output, maximum_genotype_count);
        suffix.pop_back();
    }
    if (output.size() != *expected) {
        throw std::logic_error("VCF genotype enumeration cardinality mismatch");
    }
    return output;
}

GenotypeEvidence make_count_genotype_evidence(
    const std::vector<std::uint32_t>& allele_counts,
    const std::uint32_t ambiguous_observations,
    const double error_probability
) {
    if (allele_counts.empty()) {
        throw std::invalid_argument("allele_counts must contain at least the reference allele");
    }
    if (!std::isfinite(error_probability) || error_probability <= 0.0 || error_probability >= 1.0) {
        throw std::invalid_argument("error_probability must be finite and strictly between zero and one");
    }

    GenotypeEvidence evidence;
    evidence.allele_count = allele_counts.size();
    evidence.allele_depths = allele_counts;

    if (allele_counts.size() == 1U) {
        if (allele_counts[0U] > 0U) {
            evidence.buckets.push_back({allele_counts[0U], {1.0}});
        }
    } else {
        const double off_target = error_probability / static_cast<double>(allele_counts.size() - 1U);
        for (std::size_t allele = 0U; allele < allele_counts.size(); ++allele) {
            const auto count = allele_counts[allele];
            if (count == 0U) {
                continue;
            }
            std::vector<double> likelihoods(allele_counts.size(), off_target);
            likelihoods[allele] = 1.0 - error_probability;
            evidence.buckets.push_back({count, std::move(likelihoods)});
        }
    }
    if (ambiguous_observations > 0U) {
        evidence.buckets.push_back({
            ambiguous_observations,
            std::vector<double>(allele_counts.size(), 1.0 / static_cast<double>(allele_counts.size()))
        });
    }
    if (evidence.buckets.empty()) {
        throw std::invalid_argument("count evidence must contain at least one observation");
    }
    if (const auto error = validate_genotype_evidence(evidence); error.has_value()) {
        throw std::invalid_argument(*error);
    }
    return evidence;
}

ProbabilisticGenotypeResult call_probabilistic_genotype(
    const GenotypeEvidence& evidence,
    const ProbabilisticGenotypingOptions& options
) {
    if (const auto error = validate_genotype_evidence(evidence); error.has_value()) {
        throw std::invalid_argument(*error);
    }
    validate_probabilistic_genotyping_options(options, evidence.allele_count);
    const auto genotypes = enumerate_vcf_genotypes(
        evidence.allele_count,
        options.ploidy,
        options.maximum_genotype_count
    );
    const auto priors = normalized_priors(options, genotypes.size());

    ProbabilisticGenotypeResult result;
    result.log10_likelihoods.reserve(genotypes.size());
    std::vector<double> log10_joint;
    log10_joint.reserve(genotypes.size());

    for (std::size_t genotype_index = 0U; genotype_index < genotypes.size(); ++genotype_index) {
        double log10_likelihood = 0.0;
        bool impossible = false;
        for (const auto& bucket : evidence.buckets) {
            const double probability = genotype_bucket_probability(genotypes[genotype_index], bucket);
            if (!(probability > 0.0) || !std::isfinite(probability)) {
                impossible = true;
                break;
            }
            log10_likelihood += static_cast<double>(bucket.observations) * std::log10(probability);
        }
        if (impossible) {
            log10_likelihood = -std::numeric_limits<double>::infinity();
        }
        result.log10_likelihoods.push_back(log10_likelihood);
        const double prior = priors[genotype_index];
        log10_joint.push_back(prior > 0.0 && std::isfinite(log10_likelihood)
            ? log10_likelihood + std::log10(prior)
            : -std::numeric_limits<double>::infinity());
    }

    const double best_joint = *std::max_element(log10_joint.begin(), log10_joint.end());
    if (!std::isfinite(best_joint)) {
        throw std::invalid_argument("all genotype posterior states are impossible under the supplied evidence/prior model");
    }

    result.posterior_probabilities.resize(genotypes.size(), 0.0);
    double posterior_sum = 0.0;
    for (std::size_t index = 0U; index < log10_joint.size(); ++index) {
        if (std::isfinite(log10_joint[index])) {
            result.posterior_probabilities[index] = std::pow(10.0, log10_joint[index] - best_joint);
            posterior_sum += result.posterior_probabilities[index];
        }
    }
    if (!(posterior_sum > 0.0) || !std::isfinite(posterior_sum)) {
        throw std::logic_error("posterior normalization failed");
    }
    for (double& posterior : result.posterior_probabilities) {
        posterior /= posterior_sum;
    }

    result.best_genotype_index = static_cast<std::size_t>(std::distance(
        result.posterior_probabilities.begin(),
        std::max_element(result.posterior_probabilities.begin(), result.posterior_probabilities.end())
    ));
    result.best_posterior_probability = result.posterior_probabilities[result.best_genotype_index];

    for (const std::int16_t allele : genotypes[result.best_genotype_index]) {
        result.call.allele_indices.push_back(allele);
    }
    for (std::size_t separator = 1U; separator < options.ploidy; ++separator) {
        result.call.phased_separators.push_back(0U);
    }
    result.call.depth = checked_observation_count(evidence);
    for (const std::uint32_t depth : evidence.allele_depths) {
        result.call.allele_depths.push_back(depth);
    }

    const double best_log10_likelihood = *std::max_element(
        result.log10_likelihoods.begin(), result.log10_likelihoods.end()
    );
    for (const double likelihood : result.log10_likelihoods) {
        result.call.phred_likelihoods.push_back(phred_likelihood_delta(
            best_log10_likelihood,
            likelihood,
            options.maximum_phred_likelihood
        ));
    }
    result.call.genotype_quality = phred_from_probability_error(
        1.0 - result.best_posterior_probability,
        options.maximum_genotype_quality
    );

    if (const auto error = validate_genotype_call(result.call, evidence.allele_count); error.has_value()) {
        throw std::logic_error("probabilistic genotyper emitted invalid GenotypeCall: " + *error);
    }
    return result;
}

ProbabilisticGenotypeResult genotype_variant(
    const VariantRecord& variant,
    const GenotypeEvidence& evidence,
    const ProbabilisticGenotypingOptions& options
) {
    if (const auto error = validate_variant_record(variant); error.has_value()) {
        throw std::invalid_argument("cannot genotype invalid VariantRecord: " + *error);
    }
    const std::size_t allele_count = 1U + variant.alternates.size();
    if (evidence.allele_count != allele_count) {
        throw std::invalid_argument("genotype evidence allele_count does not match VariantRecord REF/ALT cardinality");
    }
    return call_probabilistic_genotype(evidence, options);
}

ProbabilisticGenotypeResult genotype_indel_candidate(
    const IndelCandidate& candidate,
    const ProbabilisticGenotypingOptions& options,
    const double realignment_error_probability
) {
    if (candidate.variant.alternates.size() != 1U) {
        throw std::invalid_argument("Iteration 057 indel genotyping adapter requires a biallelic candidate");
    }
    const std::uint64_t classified =
        static_cast<std::uint64_t>(candidate.evidence.reference_support) +
        static_cast<std::uint64_t>(candidate.evidence.alternate_support) +
        static_cast<std::uint64_t>(candidate.evidence.ambiguous_support);
    if (classified != candidate.evidence.informative_realignments) {
        throw std::invalid_argument("Iteration 057 indel evidence accounting is inconsistent");
    }
    const auto evidence = make_count_genotype_evidence(
        {candidate.evidence.reference_support, candidate.evidence.alternate_support},
        candidate.evidence.ambiguous_support,
        realignment_error_probability
    );
    return genotype_variant(candidate.variant, evidence, options);
}

}  // namespace biocore::domain
