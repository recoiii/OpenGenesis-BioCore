#include "biocore/domain/genotype.hpp"

#include <algorithm>
#include <limits>

namespace biocore::domain {

bool GenotypeCall::fully_missing() const noexcept {
    if (allele_indices.empty()) {
        return true;
    }
    return std::all_of(
        allele_indices.values().begin(),
        allele_indices.values().end(),
        [](const std::int16_t allele) { return allele == missing_allele_index; }
    );
}

bool GenotypeCall::partially_missing() const noexcept {
    if (allele_indices.empty()) {
        return false;
    }
    const auto values = allele_indices.values();
    const bool any_missing = std::any_of(
        values.begin(), values.end(), [](const std::int16_t allele) { return allele == missing_allele_index; }
    );
    return any_missing && !fully_missing();
}

std::optional<std::size_t> genotype_likelihood_count(
    const std::size_t allele_count,
    const std::size_t ploidy
) noexcept {
    if (allele_count == 0U || ploidy == 0U) {
        return std::nullopt;
    }
    if (allele_count > std::numeric_limits<std::size_t>::max() - ploidy + 1U) {
        return std::nullopt;
    }

    const std::size_t n = allele_count + ploidy - 1U;
    const std::size_t k = std::min(ploidy, n - ploidy);
    std::size_t result = 1U;
    for (std::size_t index = 1U; index <= k; ++index) {
        const std::size_t factor = n - k + index;
        if (result > std::numeric_limits<std::size_t>::max() / factor) {
            return std::nullopt;
        }
        result *= factor;
        result /= index;
    }
    return result;
}

std::optional<std::string> validate_genotype_call(
    const GenotypeCall& call,
    const std::size_t allele_count
) {
    if (allele_count == 0U) {
        return "allele_count must include at least the reference allele";
    }
    if (call.ploidy() == 0U) {
        return "genotype call must contain at least one allele slot";
    }
    for (const std::int16_t allele : call.allele_indices.values()) {
        if (allele < missing_allele_index) {
            return "allele index is below the missing-value sentinel";
        }
        if (allele != missing_allele_index && static_cast<std::size_t>(allele) >= allele_count) {
            return "allele index exceeds the REF/ALT allele count";
        }
    }

    const std::size_t expected_separators = call.ploidy() - 1U;
    if (call.phased_separators.size() != expected_separators) {
        return "phasing separator count must equal ploidy minus one";
    }
    for (const std::uint8_t separator : call.phased_separators.values()) {
        if (separator > 1U) {
            return "phasing separator values must be 0 (/) or 1 (|)";
        }
    }

    if (!call.allele_depths.empty() && call.allele_depths.size() != allele_count) {
        return "AD cardinality must be Number=R (REF plus all ALT alleles)";
    }

    if (!call.phred_likelihoods.empty()) {
        const auto expected = genotype_likelihood_count(allele_count, call.ploidy());
        if (!expected.has_value()) {
            return "PL Number=G cardinality overflow";
        }
        if (call.phred_likelihoods.size() != *expected) {
            return "PL cardinality must be Number=G for the allele count and ploidy";
        }
        const auto values = call.phred_likelihoods.values();
        if (*std::min_element(values.begin(), values.end()) != 0U) {
            return "canonical PL values must be normalized so the minimum is zero";
        }
    }
    return std::nullopt;
}

void normalize_phred_likelihoods(GenotypeCall& call) noexcept {
    if (call.phred_likelihoods.empty()) {
        return;
    }
    auto values = call.phred_likelihoods.values();
    const std::uint32_t minimum = *std::min_element(values.begin(), values.end());
    for (std::uint32_t& value : values) {
        value -= minimum;
    }
}

}  // namespace biocore::domain
