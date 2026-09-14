#include "biocore/domain/variant_filtering.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace biocore::domain;

[[nodiscard]] bool require(const bool condition, const char* message) {
    if (!condition) std::cerr << "variant_filtering_integration_tests: " << message << '\n';
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
    for (int index = 0; index < 3; ++index) {
        reads.push_back(insertion_read(*contig, reference_sequence, false, "alt-fwd-" + std::to_string(index)));
        reads.push_back(insertion_read(*contig, reference_sequence, true, "alt-rev-" + std::to_string(index)));
    }
    for (int index = 0; index < 4; ++index) {
        reads.push_back(reference_read(*contig, reference_sequence, "ref-" + std::to_string(index)));
    }

    IndelCandidateOptions candidate_options;
    candidate_options.minimum_seed_observations = 2U;
    candidate_options.minimum_realign_support = 2U;
    const auto result = call_indel_candidates(reads, reference, candidate_options);
    if (!require(result.candidates.size() == 1U, "expected one indel candidate")) return EXIT_FAILURE;
    const auto& candidate = result.candidates.front();

    const auto genotype = genotype_indel_candidate(candidate);
    const auto evidence = make_indel_filter_evidence(candidate, genotype);

    AdvancedVariantFilterPolicy policy;
    policy.minimum_depth = 8U;
    policy.minimum_variant_allele_fraction = 0.40;
    policy.minimum_genotype_quality = 20U;
    policy.minimum_mapping_quality = 50.0;
    policy.minimum_alternate_strand_fraction = 0.20;
    policy.minimum_strand_observations = 4U;
    policy.maximum_homopolymer_run_length = 10U;
    policy.required_regions = VariantRegionSet({candidate.variant.locus});

    const auto decision = evaluate_variant_filter(candidate.variant, evidence, 0U, policy);
    ok = require(decision.passed, "057->058->059 indel integration should pass configured policy") && ok;
    ok = require(genotype.call.depth.has_value() && evidence.depth == genotype.call.depth,
                 "059 must preserve 058 DP semantics") && ok;
    ok = require(evidence.alternate_depths.size() == 1U && evidence.alternate_depths[0U] == candidate.evidence.alternate_support,
                 "059 must preserve 057/058 ALT evidence semantics") && ok;
    ok = require(evidence.alternate_strand.size() == 1U && evidence.alternate_strand[0U].has_value() &&
                 evidence.alternate_strand[0U]->forward == candidate.evidence.alternate_forward &&
                 evidence.alternate_strand[0U]->reverse == candidate.evidence.alternate_reverse,
                 "059 must preserve 057 strand evidence") && ok;
    ok = require(evidence.alternate_homopolymer.size() == 1U && evidence.alternate_homopolymer[0U].has_value() &&
                 evidence.alternate_homopolymer[0U]->run_length == candidate.evidence.homopolymer_run_length,
                 "059 must preserve 057 homopolymer evidence") && ok;

    const std::vector<VariantFilterReason> expected_order{};
    ok = require(decision.reasons == expected_order, "passing filter must have deterministic empty reason list") && ok;
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
