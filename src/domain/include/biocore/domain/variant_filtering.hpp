#pragma once

#include "biocore/domain/indel_candidate.hpp"
#include "biocore/domain/probabilistic_genotyping.hpp"
#include "biocore/domain/variant_record.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace biocore::domain {

struct AlternateStrandEvidence final {
    std::uint32_t forward{0U};
    std::uint32_t reverse{0U};
};

struct AlternateHomopolymerEvidence final {
    std::uint32_t run_length{0U};
    std::optional<char> base;
};

struct VariantFilterEvidence final {
    std::optional<std::uint32_t> depth;
    std::vector<std::optional<std::uint32_t>> alternate_depths;
    std::optional<std::uint32_t> genotype_quality;
    std::optional<double> mapping_quality;
    std::vector<std::optional<AlternateStrandEvidence>> alternate_strand;
    std::vector<std::optional<AlternateHomopolymerEvidence>> alternate_homopolymer;
};

class VariantRegionSet final {
public:
    VariantRegionSet() = default;
    explicit VariantRegionSet(std::vector<GenomicInterval> intervals);

    [[nodiscard]] bool empty() const noexcept { return intervals_.empty(); }
    [[nodiscard]] bool overlaps(const GenomicInterval& interval) const noexcept;
    [[nodiscard]] const std::vector<GenomicInterval>& intervals() const noexcept { return intervals_; }

private:
    std::vector<GenomicInterval> intervals_;
};

struct AdvancedVariantFilterPolicy final {
    std::optional<std::uint32_t> minimum_depth;
    std::optional<double> minimum_variant_allele_fraction;
    std::optional<std::uint32_t> minimum_genotype_quality;
    std::optional<double> minimum_mapping_quality;
    std::optional<double> minimum_alternate_strand_fraction;
    std::uint32_t minimum_strand_observations{4U};
    std::optional<std::uint32_t> maximum_homopolymer_run_length;
    VariantRegionSet required_regions;
    VariantRegionSet excluded_regions;
};

enum class VariantFilterReason : std::uint8_t {
    missing_depth,
    low_depth,
    missing_variant_allele_fraction,
    low_variant_allele_fraction,
    missing_genotype_quality,
    low_genotype_quality,
    missing_mapping_quality,
    low_mapping_quality,
    missing_strand_evidence,
    strand_bias,
    missing_homopolymer_evidence,
    homopolymer_run,
    outside_required_region,
    excluded_region
};

struct VariantFilterDecision final {
    std::size_t alternate_index{0U};
    bool passed{false};
    std::optional<double> variant_allele_fraction;
    std::optional<double> alternate_strand_fraction;
    std::vector<VariantFilterReason> reasons;
};

void validate_advanced_variant_filter_policy(const AdvancedVariantFilterPolicy& policy);
[[nodiscard]] std::optional<std::string_view> validate_variant_filter_evidence(
    const VariantRecord& variant,
    const VariantFilterEvidence& evidence
) noexcept;

[[nodiscard]] VariantFilterEvidence make_variant_filter_evidence(
    const VariantRecord& variant,
    const ProbabilisticGenotypeResult& genotype
);

[[nodiscard]] VariantFilterEvidence make_indel_filter_evidence(
    const IndelCandidate& candidate,
    const ProbabilisticGenotypeResult& genotype
);

[[nodiscard]] VariantFilterDecision evaluate_variant_filter(
    const VariantRecord& variant,
    const VariantFilterEvidence& evidence,
    std::size_t alternate_index,
    const AdvancedVariantFilterPolicy& policy
);

[[nodiscard]] std::string_view variant_filter_reason_name(VariantFilterReason reason) noexcept;

}  // namespace biocore::domain
