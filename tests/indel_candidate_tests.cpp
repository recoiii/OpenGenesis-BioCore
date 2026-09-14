#include "biocore/domain/indel_candidate.hpp"

#include <cstdint>
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

[[nodiscard]] ReadAlignment insertion_read(
    const ContigId contig,
    const std::string& reference,
    const bool reverse,
    const std::string& name
) {
    ReadAlignment read;
    read.read_name = name;
    read.contig_id = contig;
    read.reference_start = 1U;
    read.mapping_quality = 60U;
    read.reverse_strand = reverse;
    read.cigar = parse_alignment_cigar("4M1I8M");
    read.sequence = reference.substr(1U, 4U) + "A" + reference.substr(5U, 8U);
    read.base_qualities.assign(read.sequence.size(), 35U);
    return read;
}

[[nodiscard]] ReadAlignment reference_read(
    const ContigId contig,
    const std::string& reference
) {
    ReadAlignment read;
    read.read_name = "reference";
    read.contig_id = contig;
    read.reference_start = 1U;
    read.mapping_quality = 60U;
    read.cigar = parse_alignment_cigar("12M");
    read.sequence = reference.substr(1U, 12U);
    read.base_qualities.assign(read.sequence.size(), 35U);
    return read;
}

}  // namespace

int main() {
    using namespace biocore::domain;
    std::istringstream fasta{">chr1\nTTTAAAAAACCCGGGTTT\n"};
    const ReferenceGenome reference = ReferenceGenome::from_fasta(fasta, ReferenceAssembly::custom);
    const auto contig = reference.contigs().resolve("chr1");
    if (!require(contig.has_value(), "chr1 was not resolved")) return 1;
    const std::string reference_sequence = "TTTAAAAAACCCGGGTTT";

    std::vector<ReadAlignment> reads;
    reads.push_back(insertion_read(*contig, reference_sequence, false, "forward-seed"));
    reads.push_back(insertion_read(*contig, reference_sequence, true, "reverse-seed"));
    reads.push_back(reference_read(*contig, reference_sequence));

    IndelCandidateOptions options;
    options.minimum_seed_observations = 2U;
    options.minimum_realign_support = 2U;
    const auto result = call_indel_candidates(reads, reference, options);

    if (!require(result.statistics.total_alignments == 3U, "unexpected total alignment count")) return 1;
    if (!require(result.statistics.eligible_alignments == 3U, "unexpected eligible alignment count")) return 1;
    if (!require(result.statistics.insertion_seed_observations == 2U, "insertion seeds were not counted")) return 1;
    if (!require(result.statistics.emitted_candidates == 1U && result.candidates.size() == 1U,
                 "expected exactly one insertion candidate")) return 1;

    const auto& candidate = result.candidates.front();
    if (!require(candidate.variant.alternates.size() == 1U, "candidate is not biallelic")) return 1;
    if (!require(candidate.variant.alternates[0U].type == VariantType::insertion, "candidate is not insertion type")) return 1;
    if (!require(candidate.evidence.seed_forward == 1U && candidate.evidence.seed_reverse == 1U,
                 "seed strand evidence was not preserved")) return 1;
    if (!require(candidate.evidence.alternate_forward == 1U && candidate.evidence.alternate_reverse == 1U,
                 "realignment strand evidence was not preserved")) return 1;
    if (!require(candidate.evidence.alternate_support >= 2U, "alternate realignment support is missing")) return 1;
    if (!require(candidate.evidence.reference_support >= 1U, "reference realignment support is missing")) return 1;
    if (!require(candidate.evidence.homopolymer_run_length >= 6U && candidate.evidence.homopolymer_base == 'A',
                 "homopolymer evidence was not recorded")) return 1;

    const auto validation_error = validate_variant_record(candidate.variant);
    if (!require(!validation_error.has_value(), "emitted candidate violates shared variant model")) return 1;
    return 0;
}
