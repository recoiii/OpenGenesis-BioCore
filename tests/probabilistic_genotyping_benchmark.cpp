#include "biocore/domain/probabilistic_genotyping.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>

int main() {
    using namespace biocore::domain;
    constexpr std::size_t iterations = 100000U;
    const auto evidence = make_count_genotype_evidence({18U, 17U}, 1U, 0.01);

    std::uint64_t genotype_checksum = 0U;
    std::uint64_t gq_checksum = 0U;
    const auto begin = std::chrono::steady_clock::now();
    for (std::size_t index = 0U; index < iterations; ++index) {
        const auto result = call_probabilistic_genotype(evidence);
        genotype_checksum += result.best_genotype_index;
        gq_checksum += result.call.genotype_quality.value_or(0U);
    }
    const auto end = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();

    std::cout << "genotypes=" << iterations << '\n';
    std::cout << "alleles=2\n";
    std::cout << "ploidy=2\n";
    std::cout << "number_g=3\n";
    std::cout << "best_genotype_index=1\n";
    std::cout << "genotype_checksum=" << genotype_checksum << '\n';
    std::cout << "gq_checksum=" << gq_checksum << '\n';
    std::cout << "elapsed_ms=" << elapsed_ms << '\n';

    if (genotype_checksum != iterations) {
        return 1;
    }
    return 0;
}
