#include "biocore/domain/indel_candidate.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

int main() {
    using namespace biocore::domain;
    constexpr std::size_t read_count = 10000U;
    std::string sequence;
    sequence.reserve(2000U);
    for (std::size_t i = 0U; i < 500U; ++i) sequence += "ACGT";
    std::istringstream fasta{">chr1\n" + sequence + "\n"};
    const ReferenceGenome reference = ReferenceGenome::from_fasta(fasta, ReferenceAssembly::custom);
    const auto contig = reference.contigs().resolve("chr1");
    if (!contig.has_value()) return 1;

    std::vector<ReadAlignment> reads;
    reads.reserve(read_count);
    for (std::size_t i = 0U; i < read_count; ++i) {
        const std::uint64_t start = 100U;
        ReadAlignment read;
        read.read_name = "read-" + std::to_string(i);
        read.contig_id = *contig;
        read.reference_start = start;
        read.mapping_quality = 60U;
        read.reverse_strand = (i & 1U) != 0U;
        if (i < 100U) {
            read.cigar = parse_alignment_cigar("30M1I30M");
            read.sequence = sequence.substr(static_cast<std::size_t>(start), 30U) + "A" +
                sequence.substr(static_cast<std::size_t>(start + 30U), 30U);
        } else {
            read.cigar = parse_alignment_cigar("60M");
            read.sequence = sequence.substr(static_cast<std::size_t>(start), 60U);
        }
        read.base_qualities.assign(read.sequence.size(), 35U);
        reads.push_back(std::move(read));
    }

    IndelCandidateOptions options;
    options.minimum_seed_observations = 1U;
    options.minimum_realign_support = 1U;
    const auto begin = std::chrono::steady_clock::now();
    const auto result = call_indel_candidates(reads, reference, options);
    const auto end = std::chrono::steady_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
    if (result.statistics.total_alignments != read_count || result.statistics.insertion_seed_observations != 100U || result.candidates.empty()) {
        return 2;
    }
    std::cout << "reads=" << read_count << '\n'
              << "seed_observations=" << result.statistics.insertion_seed_observations << '\n'
              << "unique_seed_candidates=" << result.statistics.unique_seed_candidates << '\n'
              << "emitted_candidates=" << result.statistics.emitted_candidates << '\n'
              << "elapsed_ms=" << milliseconds << '\n';
    return 0;
}
