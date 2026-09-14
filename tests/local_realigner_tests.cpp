#include "biocore/domain/indel_candidate.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace biocore::domain;

[[nodiscard]] bool require(const bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

[[nodiscard]] ReadAlignment make_read(
    const ContigId contig,
    std::string name,
    const std::uint64_t start,
    const std::string& cigar,
    std::string sequence,
    const bool reverse = false
) {
    ReadAlignment read;
    read.read_name = std::move(name);
    read.contig_id = contig;
    read.reference_start = start;
    read.mapping_quality = 50U;
    read.reverse_strand = reverse;
    read.cigar = parse_alignment_cigar(cigar);
    read.sequence = std::move(sequence);
    read.base_qualities.assign(read.sequence.size(), 30U);
    return read;
}

}  // namespace

int main() {
    using namespace biocore::domain;
    std::istringstream fasta{">chr1\nACGTCAGTACGTACGT\n"};
    const ReferenceGenome reference = ReferenceGenome::from_fasta(fasta, ReferenceAssembly::custom);
    const auto contig = reference.contigs().resolve("chr1");
    if (!require(contig.has_value(), "chr1 was not resolved")) return 1;

    // Deletion of reference "AG" at positions 5..6 (0-based 4..5), anchored by C at position 4.
    // Two reads expose the deletion in CIGAR; a third carries the deletion haplotype but is represented
    // without D and is rescued by haplotype realignment.
    std::vector<ReadAlignment> reads;
    reads.push_back(make_read(*contig, "seed-fwd", 0U, "4M2D8M", "ACGT" "TACGTACG"));
    reads.push_back(make_read(*contig, "seed-rev", 0U, "4M2D8M", "ACGT" "TACGTACG", true));
    reads.push_back(make_read(*contig, "realign-only", 0U, "12M", "ACGT" "TACGTACG"));

    IndelCandidateOptions options;
    options.minimum_seed_observations = 2U;
    options.minimum_realign_support = 2U;
    const auto result = call_indel_candidates(reads, reference, options);
    if (!require(result.candidates.size() == 1U, "expected one deletion candidate")) return 1;
    const auto& candidate = result.candidates.front();
    if (!require(candidate.variant.alternates[0U].type == VariantType::deletion, "candidate is not deletion")) return 1;
    if (!require(candidate.evidence.alternate_support >= 2U, "seed reads were not supported by realignment")) return 1;
    if (!require(candidate.evidence.informative_realignments == 3U, "expected all three reads to be locally realigned")) return 1;

    // Filtered reads must not contribute to seed or realignment support.
    auto duplicate = reads.front();
    duplicate.read_name = "duplicate";
    duplicate.duplicate = true;
    reads.push_back(duplicate);
    const auto filtered = call_indel_candidates(reads, reference, options);
    if (!require(filtered.statistics.filtered_alignments == 1U, "duplicate read was not filtered")) return 1;
    if (!require(filtered.candidates.front().evidence.seed_observations == 2U,
                 "filtered read incorrectly contributed seed evidence")) return 1;

    bool malformed_rejected = false;
    try {
        static_cast<void>(parse_alignment_cigar("4M0I3M"));
    } catch (const std::invalid_argument&) {
        malformed_rejected = true;
    }
    if (!require(malformed_rejected, "zero-length CIGAR operation was accepted")) return 1;
    return 0;
}
