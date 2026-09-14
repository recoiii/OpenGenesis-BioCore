#pragma once

#include "biocore/domain/reference_genome.hpp"
#include "biocore/domain/variant_record.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace biocore::domain {

enum class AlignmentOperationKind : std::uint8_t {
    match,
    insertion,
    deletion,
    reference_skip,
    soft_clip,
    hard_clip,
    padding,
    sequence_match,
    sequence_mismatch
};

struct AlignmentOperation final {
    std::uint32_t length{0U};
    AlignmentOperationKind kind{AlignmentOperationKind::match};
};

struct ReadAlignment final {
    std::string read_name;
    ContigId contig_id{0U};
    std::uint64_t reference_start{0U};
    std::uint32_t mapping_quality{0U};
    bool reverse_strand{false};
    bool duplicate{false};
    bool secondary{false};
    bool supplementary{false};
    bool qc_failed{false};
    std::vector<AlignmentOperation> cigar;
    std::string sequence;
    std::vector<std::uint8_t> base_qualities;
};

struct IndelCandidateOptions final {
    std::uint32_t minimum_mapping_quality{20U};
    std::uint32_t minimum_base_quality{15U};
    std::uint32_t minimum_seed_observations{1U};
    std::uint32_t minimum_realign_support{2U};
    std::uint32_t minimum_score_improvement{1U};
    std::uint32_t realignment_flank{12U};
    std::uint32_t maximum_realign_read_bases{1024U};
    std::uint32_t maximum_candidate_indel_bases{512U};
};

struct IndelCandidateEvidence final {
    std::uint32_t seed_observations{0U};
    std::uint32_t seed_forward{0U};
    std::uint32_t seed_reverse{0U};
    std::uint32_t informative_realignments{0U};
    std::uint32_t alternate_support{0U};
    std::uint32_t reference_support{0U};
    std::uint32_t ambiguous_support{0U};
    std::uint32_t alternate_forward{0U};
    std::uint32_t alternate_reverse{0U};
    std::uint64_t alternate_mapping_quality_sum{0U};
    std::uint32_t homopolymer_run_length{0U};
    std::optional<char> homopolymer_base;
};

struct IndelCandidate final {
    VariantRecord variant;
    IndelCandidateEvidence evidence;
};

struct IndelCandidateStatistics final {
    std::uint64_t total_alignments{0U};
    std::uint64_t eligible_alignments{0U};
    std::uint64_t filtered_alignments{0U};
    std::uint64_t insertion_seed_observations{0U};
    std::uint64_t deletion_seed_observations{0U};
    std::uint64_t unique_seed_candidates{0U};
    std::uint64_t emitted_candidates{0U};
};

struct IndelCandidateResult final {
    IndelCandidateStatistics statistics;
    std::vector<IndelCandidate> candidates;
};

void validate_indel_candidate_options(const IndelCandidateOptions& options);

[[nodiscard]] std::vector<AlignmentOperation> parse_alignment_cigar(std::string_view cigar);

[[nodiscard]] std::optional<std::string> validate_read_alignment(
    const ReadAlignment& alignment,
    const ReferenceGenome& reference,
    const IndelCandidateOptions& options
);

[[nodiscard]] IndelCandidateResult call_indel_candidates(
    const std::vector<ReadAlignment>& alignments,
    const ReferenceGenome& reference,
    const IndelCandidateOptions& options = {}
);

}  // namespace biocore::domain
