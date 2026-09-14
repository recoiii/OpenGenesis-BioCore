#include "biocore/domain/indel_candidate.hpp"
#include "biocore/domain/variant_normalization.hpp"
#include "indel_candidate_internal.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace biocore::domain {
namespace {

struct CandidateKey final {
    ContigId contig_id{0U};
    std::uint64_t start{0U};
    std::string reference;
    std::string alternate;

    friend bool operator<(const CandidateKey& lhs, const CandidateKey& rhs) noexcept {
        return std::tie(lhs.contig_id, lhs.start, lhs.reference, lhs.alternate) <
            std::tie(rhs.contig_id, rhs.start, rhs.reference, rhs.alternate);
    }
};

struct Aggregate final {
    VariantRecord variant;
    IndelCandidateEvidence evidence;
};

[[nodiscard]] CandidateKey candidate_key(const VariantRecord& variant) {
    if (variant.alternates.size() != 1U) throw std::logic_error("indel candidate seed must be biallelic");
    return {variant.locus.contig_id, variant.locus.start, variant.reference.sequence, variant.alternates[0U].sequence};
}

[[nodiscard]] bool alignment_is_eligible(
    const ReadAlignment& alignment,
    const IndelCandidateOptions& options
) noexcept {
    if (alignment.duplicate || alignment.secondary || alignment.supplementary || alignment.qc_failed ||
        alignment.mapping_quality < options.minimum_mapping_quality) {
        return false;
    }
    std::uint64_t quality_sum = 0U;
    std::uint64_t callable = 0U;
    for (const auto quality : alignment.base_qualities) {
        if (quality == 0xFFU) continue;
        quality_sum += quality;
        ++callable;
    }
    return callable != 0U && quality_sum >= callable * options.minimum_base_quality;
}

[[nodiscard]] bool overlaps_realignment_region(
    const ReadAlignment& alignment,
    const VariantRecord& variant,
    const std::uint32_t flank
) {
    const std::uint64_t span = indel_detail::reference_span(alignment);
    const std::uint64_t read_end = alignment.reference_start + span;
    const std::uint64_t region_start = variant.locus.start > flank ? variant.locus.start - flank : 0U;
    const std::uint64_t region_end = variant.locus.end > std::numeric_limits<std::uint64_t>::max() - flank
        ? std::numeric_limits<std::uint64_t>::max()
        : variant.locus.end + flank;
    return alignment.reference_start < region_end && read_end > region_start;
}

void realign_candidate(
    Aggregate& aggregate,
    const std::vector<const ReadAlignment*>& alignments,
    const ReferenceGenome& reference,
    const IndelCandidateOptions& options
) {
    const auto contig = reference.sequence(aggregate.variant.locus.contig_id);
    if (!contig.has_value()) throw std::logic_error("candidate contig disappeared from reference");

    for (const ReadAlignment* alignment : alignments) {
        if (alignment->contig_id != aggregate.variant.locus.contig_id ||
            !overlaps_realignment_region(*alignment, aggregate.variant, options.realignment_flank)) {
            continue;
        }
        const std::uint64_t span = indel_detail::reference_span(*alignment);
        const std::uint64_t read_end = alignment->reference_start + span;
        const std::uint64_t minimum_start = std::min(alignment->reference_start, aggregate.variant.locus.start);
        const std::uint64_t window_start = minimum_start > options.realignment_flank
            ? minimum_start - options.realignment_flank
            : 0U;
        const std::uint64_t maximum_end = std::max(read_end, aggregate.variant.locus.end);
        const std::uint64_t requested_end = maximum_end > std::numeric_limits<std::uint64_t>::max() - options.realignment_flank
            ? std::numeric_limits<std::uint64_t>::max()
            : maximum_end + options.realignment_flank;
        const std::uint64_t window_end = std::min<std::uint64_t>(requested_end, contig->size());
        if (window_end <= window_start) continue;

        const auto reference_window = reference.slice(
            aggregate.variant.locus.contig_id,
            window_start,
            window_end - window_start
        );
        if (!reference_window.has_value()) throw std::logic_error("realignment reference window disappeared");
        const std::string alternate_window = indel_detail::apply_variant_to_haplotype(
            *reference_window, window_start, aggregate.variant
        );
        const std::uint32_t reference_score = indel_detail::semiglobal_edit_distance(alignment->sequence, *reference_window);
        const std::uint32_t alternate_score = indel_detail::semiglobal_edit_distance(alignment->sequence, alternate_window);
        ++aggregate.evidence.informative_realignments;
        if (alternate_score + options.minimum_score_improvement <= reference_score) {
            ++aggregate.evidence.alternate_support;
            aggregate.evidence.alternate_mapping_quality_sum += alignment->mapping_quality;
            if (alignment->reverse_strand) ++aggregate.evidence.alternate_reverse;
            else ++aggregate.evidence.alternate_forward;
        } else if (reference_score + options.minimum_score_improvement <= alternate_score) {
            ++aggregate.evidence.reference_support;
        } else {
            ++aggregate.evidence.ambiguous_support;
        }
    }
}

}  // namespace

IndelCandidateResult call_indel_candidates(
    const std::vector<ReadAlignment>& alignments,
    const ReferenceGenome& reference,
    const IndelCandidateOptions& options
) {
    validate_indel_candidate_options(options);
    IndelCandidateResult result;
    result.statistics.total_alignments = alignments.size();

    std::map<CandidateKey, Aggregate> aggregates;
    std::vector<const ReadAlignment*> eligible;
    eligible.reserve(alignments.size());

    for (const auto& alignment : alignments) {
        if (const auto error = validate_read_alignment(alignment, reference, options); error.has_value()) {
            throw std::invalid_argument("invalid read alignment: " + *error);
        }
        if (!alignment_is_eligible(alignment, options)) {
            ++result.statistics.filtered_alignments;
            continue;
        }
        ++result.statistics.eligible_alignments;
        eligible.push_back(&alignment);
        std::set<CandidateKey> observed_in_read;
        for (auto seed : indel_detail::extract_seeds(alignment, reference, options)) {
            const auto type = seed.variant.alternates[0U].type;
            if (type == VariantType::insertion) ++result.statistics.insertion_seed_observations;
            else if (type == VariantType::deletion) ++result.statistics.deletion_seed_observations;
            else throw std::logic_error("indel seed normalized to a non-indel type");

            const CandidateKey key = candidate_key(seed.variant);
            if (!observed_in_read.insert(key).second) continue;
            auto [found, inserted] = aggregates.try_emplace(key);
            if (inserted) found->second.variant = std::move(seed.variant);
            ++found->second.evidence.seed_observations;
            if (seed.reverse_strand) ++found->second.evidence.seed_reverse;
            else ++found->second.evidence.seed_forward;
        }
    }

    result.statistics.unique_seed_candidates = aggregates.size();
    for (auto& [key, aggregate] : aggregates) {
        static_cast<void>(key);
        if (aggregate.evidence.seed_observations < options.minimum_seed_observations) continue;
        realign_candidate(aggregate, eligible, reference, options);
        if (aggregate.evidence.alternate_support < options.minimum_realign_support) continue;

        const auto [run_length, run_base] = indel_detail::homopolymer_context(aggregate.variant, reference);
        aggregate.evidence.homopolymer_run_length = run_length;
        aggregate.evidence.homopolymer_base = run_base;
        normalize_variant(aggregate.variant, reference);
        if (const auto error = validate_variant_record(aggregate.variant); error.has_value()) {
            throw std::logic_error("indel candidate violates shared variant model: " + *error);
        }
        if (const auto error = validate_reference_match(aggregate.variant, reference); error.has_value()) {
            throw std::logic_error("indel candidate fails canonical reference validation: " + *error);
        }
        result.candidates.push_back({std::move(aggregate.variant), aggregate.evidence});
    }

    std::sort(result.candidates.begin(), result.candidates.end(), [](const IndelCandidate& lhs, const IndelCandidate& rhs) {
        const auto& la = lhs.variant.alternates[0U].sequence;
        const auto& ra = rhs.variant.alternates[0U].sequence;
        return std::tie(lhs.variant.locus.contig_id, lhs.variant.locus.start, lhs.variant.reference.sequence, la) <
            std::tie(rhs.variant.locus.contig_id, rhs.variant.locus.start, rhs.variant.reference.sequence, ra);
    });
    result.statistics.emitted_candidates = result.candidates.size();
    return result;
}

}  // namespace biocore::domain
