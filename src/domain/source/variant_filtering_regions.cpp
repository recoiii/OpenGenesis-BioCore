#include "biocore/domain/variant_filtering.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace biocore::domain {
namespace {

[[nodiscard]] bool finite_nonnegative(const double value) noexcept {
    return std::isfinite(value) && value >= 0.0;
}

[[nodiscard]] bool interval_less(const GenomicInterval& left, const GenomicInterval& right) noexcept {
    if (left.contig_id != right.contig_id) return left.contig_id < right.contig_id;
    if (left.start != right.start) return left.start < right.start;
    return left.end < right.end;
}

}  // namespace

VariantRegionSet::VariantRegionSet(std::vector<GenomicInterval> intervals) {
    for (const auto& interval : intervals) {
        if (!interval.valid() || interval.start == interval.end) {
            throw std::invalid_argument("variant filter regions must be non-empty valid half-open intervals");
        }
    }
    std::sort(intervals.begin(), intervals.end(), interval_less);
    intervals_.reserve(intervals.size());
    for (const auto& interval : intervals) {
        if (intervals_.empty() || intervals_.back().contig_id != interval.contig_id ||
            intervals_.back().end < interval.start) {
            intervals_.push_back(interval);
            continue;
        }
        intervals_.back().end = std::max(intervals_.back().end, interval.end);
    }
}

bool VariantRegionSet::overlaps(const GenomicInterval& interval) const noexcept {
    if (!interval.valid() || interval.start == interval.end || intervals_.empty()) {
        return false;
    }
    const auto found = std::lower_bound(
        intervals_.begin(), intervals_.end(), interval,
        [](const GenomicInterval& candidate, const GenomicInterval& query) {
            if (candidate.contig_id != query.contig_id) return candidate.contig_id < query.contig_id;
            return candidate.start < query.start;
        }
    );
    if (found != intervals_.end() && found->contig_id == interval.contig_id &&
        found->start < interval.end && interval.start < found->end) {
        return true;
    }
    if (found == intervals_.begin()) {
        return false;
    }
    const auto previous = std::prev(found);
    return previous->contig_id == interval.contig_id &&
        previous->start < interval.end && interval.start < previous->end;
}

void validate_advanced_variant_filter_policy(const AdvancedVariantFilterPolicy& policy) {
    if (policy.minimum_variant_allele_fraction.has_value()) {
        const double value = *policy.minimum_variant_allele_fraction;
        if (!std::isfinite(value) || value < 0.0 || value > 1.0) {
            throw std::invalid_argument("minimum VAF must be finite and in [0,1]");
        }
    }
    if (policy.minimum_mapping_quality.has_value() && !finite_nonnegative(*policy.minimum_mapping_quality)) {
        throw std::invalid_argument("minimum mapping quality must be finite and non-negative");
    }
    if (policy.minimum_alternate_strand_fraction.has_value()) {
        const double value = *policy.minimum_alternate_strand_fraction;
        if (!std::isfinite(value) || value < 0.0 || value > 0.5) {
            throw std::invalid_argument("minimum alternate strand fraction must be finite and in [0,0.5]");
        }
        if (policy.minimum_strand_observations == 0U) {
            throw std::invalid_argument("minimum strand observations must be positive when strand filtering is enabled");
        }
    }
}

std::optional<std::string_view> validate_variant_filter_evidence(
    const VariantRecord& variant,
    const VariantFilterEvidence& evidence
) noexcept {
    const std::size_t alternate_count = variant.alternates.size();
    if (!evidence.alternate_depths.empty() && evidence.alternate_depths.size() != alternate_count) {
        return "alternate depth cardinality must equal ALT count";
    }
    if (!evidence.alternate_strand.empty() && evidence.alternate_strand.size() != alternate_count) {
        return "alternate strand cardinality must equal ALT count";
    }
    if (!evidence.alternate_homopolymer.empty() && evidence.alternate_homopolymer.size() != alternate_count) {
        return "alternate homopolymer cardinality must equal ALT count";
    }
    if (evidence.mapping_quality.has_value() && !finite_nonnegative(*evidence.mapping_quality)) {
        return "mapping quality must be finite and non-negative";
    }
    if (evidence.depth.has_value()) {
        std::uint64_t assigned_alt_depth = 0U;
        for (const auto& depth : evidence.alternate_depths) {
            if (depth.has_value()) assigned_alt_depth += *depth;
        }
        if (assigned_alt_depth > *evidence.depth) {
            return "alternate depths exceed total depth";
        }
    }
    for (const auto& strand : evidence.alternate_strand) {
        if (!strand.has_value()) continue;
        const std::uint64_t support = static_cast<std::uint64_t>(strand->forward) + strand->reverse;
        if (support > std::numeric_limits<std::uint32_t>::max()) {
            return "alternate strand support exceeds uint32 range";
        }
    }
    for (const auto& homopolymer : evidence.alternate_homopolymer) {
        if (!homopolymer.has_value()) continue;
        if (homopolymer->base.has_value()) {
            const char base = *homopolymer->base;
            if (base != 'A' && base != 'C' && base != 'G' && base != 'T' && base != 'N') {
                return "homopolymer base must be canonical A/C/G/T/N";
            }
        }
    }
    return std::nullopt;
}

}  // namespace biocore::domain
