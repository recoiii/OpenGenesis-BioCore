#include "biocore/domain/variant_prioritization.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace biocore::domain;

[[nodiscard]] bool require(const bool condition, const char* message) {
    if (!condition) std::cerr << "variant_prioritization_integration_tests: " << message << '\n';
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
    const auto candidates = call_indel_candidates(reads, reference, candidate_options);
    if (!require(candidates.candidates.size() == 1U, "expected one real Iteration 057 candidate")) return EXIT_FAILURE;
    const auto& candidate = candidates.candidates.front();

    const auto genotype = genotype_indel_candidate(candidate);
    const auto filter_evidence = make_indel_filter_evidence(candidate, genotype);
    AdvancedVariantFilterPolicy filter_policy;
    filter_policy.minimum_depth = 8U;
    filter_policy.minimum_variant_allele_fraction = 0.40;
    filter_policy.minimum_genotype_quality = 20U;
    filter_policy.minimum_mapping_quality = 50.0;
    filter_policy.minimum_alternate_strand_fraction = 0.20;
    filter_policy.maximum_homopolymer_run_length = 10U;
    const auto filter_decision = evaluate_variant_filter(candidate.variant, filter_evidence, 0U, filter_policy);
    if (!require(filter_decision.passed, "real 057->058->059 chain did not pass filtering")) return EXIT_FAILURE;

    const auto context = make_variant_prioritization_context(candidate.variant, filter_evidence, filter_decision);
    VariantPrioritizationPolicy priority_policy;
    priority_policy.baseline_score = 1.0;
    priority_policy.rules = {
        {"passed-059", PrioritizationFeature::filter_passed, PrioritizationOperator::equal, true, 4.0, MissingFeatureBehavior::reject_evaluation},
        {"adequate-vaf", PrioritizationFeature::variant_allele_fraction, PrioritizationOperator::greater_or_equal, 0.40, 3.0, MissingFeatureBehavior::reject_evaluation},
        {"balanced-strand", PrioritizationFeature::alternate_strand_fraction, PrioritizationOperator::greater_or_equal, 0.20, 2.0, MissingFeatureBehavior::reject_evaluation},
        {"insertion", PrioritizationFeature::variant_type, PrioritizationOperator::equal, VariantType::insertion, 1.0, MissingFeatureBehavior::skip_rule}
    };
    const auto prioritized = evaluate_variant_prioritization(context, priority_policy);
    ok = require(prioritized.score == 11.0, "057->058->059->060 integration score mismatch") && ok;
    ok = require(prioritized.trace.size() == 4U, "integration trace must preserve all rules") && ok;
    for (const auto& trace : prioritized.trace) {
        ok = require(trace.status == PrioritizationRuleStatus::matched,
                     "every integration rule should match") && ok;
    }
    ok = require(context.homopolymer_run_length.has_value(), "060 context lost Iteration 057 homopolymer evidence") && ok;
    ok = require(context.alternate_strand_fraction.has_value(), "060 context lost Iteration 057 strand evidence") && ok;

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
