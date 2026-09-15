#include "biocore/domain/truth_set_benchmark.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace biocore::domain;

constexpr std::size_t truth_count = 250000U;
constexpr std::size_t omitted_every = 50U;
constexpr std::size_t added_false_positives = 5000U;

BenchmarkVariantObservation make_observation(const std::size_t index) {
    const std::string ref = "A";
    const std::string alt = (index % 11U == 0U) ? "AT" : "G";
    return {
        .contig_name = "1",
        .start = static_cast<std::uint64_t>(index * 3U),
        .end = static_cast<std::uint64_t>(index * 3U + 1U),
        .reference_allele = ref,
        .alternate_allele = alt,
        .variant_type = classify_variant(ref, alt, false),
        .symbolic = false,
        .genotype = BenchmarkGenotype{2U, static_cast<std::uint32_t>((index % 2U) + 1U)},
    };
}

}  // namespace

int main() {
    using clock = std::chrono::steady_clock;
    std::vector<BenchmarkVariantObservation> truth;
    std::vector<BenchmarkVariantObservation> candidate;
    truth.reserve(truth_count);
    candidate.reserve(truth_count + added_false_positives);
    for (std::size_t index = 0U; index < truth_count; ++index) {
        auto observation = make_observation(index);
        truth.push_back(observation);
        if (index % omitted_every != 0U) {
            candidate.push_back(std::move(observation));
        }
    }
    for (std::size_t index = 0U; index < added_false_positives; ++index) {
        const std::size_t base = truth_count + index;
        candidate.push_back(make_observation(base));
    }

    const auto started = clock::now();
    const auto result = benchmark_truth_set(truth, candidate);
    const auto finished = clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(finished - started).count();

    const std::uint64_t expected_fn = truth_count / omitted_every;
    if (result.overall.false_negatives != expected_fn ||
        result.overall.false_positives != added_false_positives ||
        result.overall.true_positives != truth_count - expected_fn ||
        result.genotypes.comparable != result.overall.true_positives ||
        result.genotypes.concordant != result.genotypes.comparable) {
        std::cerr << "truth-set performance benchmark invariant failed\n";
        return EXIT_FAILURE;
    }

    std::cout
        << "{\"truth_variants\":" << truth_count
        << ",\"candidate_variants\":" << candidate.size()
        << ",\"tp\":" << result.overall.true_positives
        << ",\"fp\":" << result.overall.false_positives
        << ",\"fn\":" << result.overall.false_negatives
        << ",\"genotype_comparable\":" << result.genotypes.comparable
        << ",\"elapsed_ms\":" << elapsed_ms
        << "}\n";
    return EXIT_SUCCESS;
}
