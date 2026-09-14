#pragma once

#include "biocore/domain/inline_values.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace biocore::domain {

inline constexpr std::int16_t missing_allele_index = -1;

struct GenotypeCall final {
    InlineValues<std::int16_t, 2U> allele_indices;
    InlineValues<std::uint8_t, 1U> phased_separators;
    std::optional<std::uint32_t> depth;
    InlineValues<std::uint32_t, 3U> allele_depths;
    std::optional<std::uint32_t> genotype_quality;
    InlineValues<std::uint32_t, 6U> phred_likelihoods;

    [[nodiscard]] std::size_t ploidy() const noexcept { return allele_indices.size(); }
    [[nodiscard]] bool fully_missing() const noexcept;
    [[nodiscard]] bool partially_missing() const noexcept;
};

[[nodiscard]] std::optional<std::size_t> genotype_likelihood_count(
    std::size_t allele_count,
    std::size_t ploidy
) noexcept;
[[nodiscard]] std::optional<std::string> validate_genotype_call(
    const GenotypeCall& call,
    std::size_t allele_count
);
void normalize_phred_likelihoods(GenotypeCall& call) noexcept;

}  // namespace biocore::domain
