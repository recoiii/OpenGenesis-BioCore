#include "biocore/domain/probabilistic_genotyping.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace biocore::domain;

[[nodiscard]] bool require(const bool condition, const char* message) {
    if (!condition) std::cerr << "probabilistic_genotyping_indel_tests: " << message << '\n';
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
    const std::string& reference,
    const std::string& name
) {
    ReadAlignment read;
    read.read_name = name;
    read.contig_id = contig;
    read.reference_start = 1U;
    read.mapping_quality = 60U;
    read.cigar = parse_alignment_cigar("12M");
    read.sequence = reference.substr(1U, 12U);
    read.base_qualities.assign(read.sequence.size(), 35U);
    return read;
}

[[nodiscard]] VariantRecord matching_snv() {
    VariantRecord record;
    record.locus = GenomicInterval{0U, 2U, 3U};
    record.reference = Allele{"A", VariantType::unknown, false};
    record.alternates.push_back(Allele{"G", VariantType::snv, false});
    return record;
}

}  // namespace

int main() {
    using namespace biocore::domain;
    bool ok = true;

    std::istringstream fasta{">chr1\nTTTAAAAAACCCGGGTTT\n"};
    const ReferenceGenome reference = ReferenceGenome::from_fasta(fasta, ReferenceAssembly::custom);
    const auto contig = reference.contigs().resolve("chr1");
    if (!require(contig.has_value(), "chr1 was not resolved")) return EXIT_FAILURE;
    const std::string reference_sequence = "TTTAAAAAACCCGGGTTT";

    std::vector<ReadAlignment> reads;
    reads.push_back(insertion_read(*contig, reference_sequence, false, "alt-fwd-1"));
    reads.push_back(insertion_read(*contig, reference_sequence, true, "alt-rev-1"));
    reads.push_back(insertion_read(*contig, reference_sequence, false, "alt-fwd-2"));
    reads.push_back(insertion_read(*contig, reference_sequence, true, "alt-rev-2"));
    reads.push_back(reference_read(*contig, reference_sequence, "ref-1"));
    reads.push_back(reference_read(*contig, reference_sequence, "ref-2"));
    reads.push_back(reference_read(*contig, reference_sequence, "ref-3"));
    reads.push_back(reference_read(*contig, reference_sequence, "ref-4"));

    IndelCandidateOptions candidate_options;
    candidate_options.minimum_seed_observations = 2U;
    candidate_options.minimum_realign_support = 2U;
    const auto candidates = call_indel_candidates(reads, reference, candidate_options);
    if (!require(candidates.candidates.size() == 1U, "expected one insertion candidate")) return EXIT_FAILURE;
    const auto& candidate = candidates.candidates.front();
    ok = require(candidate.evidence.alternate_support == 4U && candidate.evidence.reference_support == 4U,
                 "fixture must provide balanced indel evidence") && ok;

    const auto indel_call = genotype_indel_candidate(candidate);
    ok = require(indel_call.call.allele_indices.size() == 2U &&
                 indel_call.call.allele_indices[0U] == 0 && indel_call.call.allele_indices[1U] == 1,
                 "balanced indel evidence calls heterozygous genotype") && ok;
    ok = require(indel_call.call.depth == candidate.evidence.informative_realignments,
                 "indel DP derives from informative local realignments") && ok;
    ok = require(indel_call.call.allele_depths.size() == 2U &&
                 indel_call.call.allele_depths[0U] == candidate.evidence.reference_support &&
                 indel_call.call.allele_depths[1U] == candidate.evidence.alternate_support,
                 "indel AD derives from genotype-neutral REF/ALT evidence") && ok;

    const VariantRecord snv = matching_snv();
    const auto same_evidence = make_count_genotype_evidence(
        {candidate.evidence.reference_support, candidate.evidence.alternate_support},
        candidate.evidence.ambiguous_support,
        0.01
    );
    const auto snv_call = genotype_variant(snv, same_evidence);
    ok = require(snv_call.call.allele_indices.values().size() == indel_call.call.allele_indices.values().size(),
                 "SNV and indel calls have same ploidy") && ok;
    ok = require(snv_call.call.allele_indices[0U] == indel_call.call.allele_indices[0U] &&
                 snv_call.call.allele_indices[1U] == indel_call.call.allele_indices[1U],
                 "SNV and indel use the same genotype decision path") && ok;
    ok = require(snv_call.call.phred_likelihoods.values().size() == indel_call.call.phred_likelihoods.values().size(),
                 "SNV and indel PL cardinality matches") && ok;
    for (std::size_t index = 0U; index < snv_call.call.phred_likelihoods.size(); ++index) {
        ok = require(snv_call.call.phred_likelihoods[index] == indel_call.call.phred_likelihoods[index],
                     "SNV and indel identical evidence yields identical PL") && ok;
    }

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
