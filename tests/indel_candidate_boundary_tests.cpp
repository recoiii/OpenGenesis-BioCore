#include "biocore/domain/indel_candidate.hpp"
#include "biocore/domain/vcf_ingestion.hpp"

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

[[nodiscard]] ReadAlignment make_insertion_read(
    const ContigId contig,
    const std::string& reference,
    const bool reverse
) {
    ReadAlignment read;
    read.read_name = reverse ? "reverse" : "forward";
    read.contig_id = contig;
    read.reference_start = 0U;
    read.mapping_quality = 60U;
    read.reverse_strand = reverse;
    read.cigar = parse_alignment_cigar("5M2I7M");
    read.sequence = reference.substr(0U, 5U) + "GG" + reference.substr(5U, 7U);
    read.base_qualities.assign(read.sequence.size(), 40U);
    return read;
}

}  // namespace

int main() {
    using namespace biocore::domain;
    const std::string reference_sequence = "ACGTACGTACGTACGT";
    std::istringstream fasta{">chr1\n" + reference_sequence + "\n"};
    const ReferenceGenome reference = ReferenceGenome::from_fasta(fasta, ReferenceAssembly::custom);
    const auto contig = reference.contigs().resolve("chr1");
    if (!require(contig.has_value(), "chr1 was not resolved")) return 1;

    std::vector<ReadAlignment> reads{
        make_insertion_read(*contig, reference_sequence, false),
        make_insertion_read(*contig, reference_sequence, true)
    };
    IndelCandidateOptions options;
    options.minimum_seed_observations = 2U;
    options.minimum_realign_support = 2U;
    const auto result = call_indel_candidates(reads, reference, options);
    if (!require(result.candidates.size() == 1U, "expected exactly one insertion candidate")) return 1;
    const auto& emitted = result.candidates.front().variant;
    const auto contig_name = reference.contigs().canonical_name(emitted.locus.contig_id);
    if (!require(contig_name.has_value(), "candidate contig name was lost")) return 1;

    std::ostringstream vcf;
    vcf << "##fileformat=VCFv4.5\n"
        << "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\n"
        << *contig_name << '\t' << internal_start_to_vcf_position(emitted.locus.start)
        << "\t.\t" << emitted.reference.sequence << '\t' << emitted.alternates[0U].sequence
        << "\t.\tPASS\t.\n";
    std::istringstream input{vcf.str()};
    const auto ingested = ingest_vcf(input, reference);
    if (!require(ingested.records.size() == 1U, "056 canonical ingestion rejected caller output")) return 1;
    const auto& roundtrip = ingested.records.front().variant;
    if (!require(roundtrip.locus.contig_id == emitted.locus.contig_id &&
                 roundtrip.locus.start == emitted.locus.start &&
                 roundtrip.locus.end == emitted.locus.end &&
                 roundtrip.reference.sequence == emitted.reference.sequence &&
                 roundtrip.alternates.size() == 1U &&
                 roundtrip.alternates[0U].sequence == emitted.alternates[0U].sequence,
                 "caller output changed at the Iteration 056 canonical boundary")) return 1;
    return 0;
}
