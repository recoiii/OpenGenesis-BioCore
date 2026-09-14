#include "biocore/domain/variant_filtering.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace biocore::domain;

[[nodiscard]] bool require(const bool condition, const char* message) {
    if (!condition) std::cerr << "variant_filtering_context_tests: " << message << '\n';
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

[[nodiscard]] bool has_reason(const VariantFilterDecision& decision, const VariantFilterReason reason) {
    for (const auto observed : decision.reasons) if (observed == reason) return true;
    return false;
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

    std::vector<ReadAlignment> balanced_reads;
    balanced_reads.push_back(insertion_read(*contig, reference_sequence, false, "alt-fwd-1"));
    balanced_reads.push_back(insertion_read(*contig, reference_sequence, true, "alt-rev-1"));
    balanced_reads.push_back(insertion_read(*contig, reference_sequence, false, "alt-fwd-2"));
    balanced_reads.push_back(insertion_read(*contig, reference_sequence, true, "alt-rev-2"));
    balanced_reads.push_back(reference_read(*contig, reference_sequence, "ref-1"));
    balanced_reads.push_back(reference_read(*contig, reference_sequence, "ref-2"));

    IndelCandidateOptions candidate_options;
    candidate_options.minimum_seed_observations = 2U;
    candidate_options.minimum_realign_support = 2U;
    const auto called = call_indel_candidates(balanced_reads, reference, candidate_options);
    if (!require(called.candidates.size() == 1U, "expected one balanced indel candidate")) return EXIT_FAILURE;
    const auto& candidate = called.candidates.front();
    const auto genotype = genotype_indel_candidate(candidate);
    const auto evidence = make_indel_filter_evidence(candidate, genotype);

    AdvancedVariantFilterPolicy context_policy;
    context_policy.minimum_alternate_strand_fraction = 0.20;
    context_policy.minimum_strand_observations = 4U;
    context_policy.maximum_homopolymer_run_length = 10U;
    context_policy.minimum_mapping_quality = 50.0;
    const auto balanced = evaluate_variant_filter(candidate.variant, evidence, 0U, context_policy);
    ok = require(balanced.passed, "balanced strand evidence should pass context policy") && ok;
    ok = require(balanced.alternate_strand_fraction.has_value() && *balanced.alternate_strand_fraction == 0.5,
                 "balanced ALT strand fraction must be 0.5") && ok;
    ok = require(evidence.mapping_quality.has_value() && *evidence.mapping_quality == 60.0,
                 "indel MQ fallback must derive from alternate-supporting read MQ") && ok;

    VariantFilterEvidence biased = evidence;
    biased.alternate_strand[0U] = AlternateStrandEvidence{8U, 0U};
    const auto biased_result = evaluate_variant_filter(candidate.variant, biased, 0U, context_policy);
    ok = require(has_reason(biased_result, VariantFilterReason::strand_bias),
                 "one-sided ALT support must trigger strand bias") && ok;

    VariantFilterEvidence low_support = evidence;
    low_support.alternate_strand[0U] = AlternateStrandEvidence{2U, 0U};
    const auto low_support_result = evaluate_variant_filter(candidate.variant, low_support, 0U, context_policy);
    ok = require(!has_reason(low_support_result, VariantFilterReason::strand_bias),
                 "strand bias must not hard-filter below minimum strand observation floor") && ok;
    ok = require(low_support_result.alternate_strand_fraction.has_value() &&
                 *low_support_result.alternate_strand_fraction == 0.0,
                 "below-floor strand evidence should retain the observed fraction without triggering bias") && ok;

    AdvancedVariantFilterPolicy homopolymer_policy;
    homopolymer_policy.maximum_homopolymer_run_length = 4U;
    const auto homopolymer = evaluate_variant_filter(candidate.variant, evidence, 0U, homopolymer_policy);
    ok = require(has_reason(homopolymer, VariantFilterReason::homopolymer_run),
                 "long homopolymer context must trigger configured filter") && ok;

    VariantFilterEvidence missing_context = make_variant_filter_evidence(candidate.variant, genotype);
    const auto missing = evaluate_variant_filter(candidate.variant, missing_context, 0U, context_policy);
    ok = require(has_reason(missing, VariantFilterReason::missing_strand_evidence),
                 "requested strand filter must fail closed when evidence is absent") && ok;
    ok = require(has_reason(missing, VariantFilterReason::missing_homopolymer_evidence),
                 "requested homopolymer filter must fail closed when evidence is absent") && ok;

    IndelCandidate inconsistent = candidate;
    inconsistent.evidence.alternate_forward += 1U;
    bool inconsistent_rejected = false;
    try {
        static_cast<void>(make_indel_filter_evidence(inconsistent, genotype));
    } catch (const std::invalid_argument&) {
        inconsistent_rejected = true;
    }
    ok = require(inconsistent_rejected, "inconsistent Iteration 057 strand evidence was accepted") && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
