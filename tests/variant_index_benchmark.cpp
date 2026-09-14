#include "biocore/domain/variant_index.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

int main() {
    using biocore::domain::VariantCoordinateIndex;
    using biocore::domain::VariantIndexEntry;
    using Clock = std::chrono::steady_clock;

    constexpr std::size_t record_count = 1000000U;
    constexpr std::size_t query_count = 10000U;

    std::vector<VariantIndexEntry> entries;
    entries.reserve(record_count);
    for (std::size_t index = 0U; index < record_count; ++index) {
        const std::uint64_t start = static_cast<std::uint64_t>(index) * 3U;
        entries.push_back(VariantIndexEntry{{0U, start, start + 1U}, index});
    }

    const auto build_begin = Clock::now();
    const auto coordinate_index = VariantCoordinateIndex::build(entries);
    const auto build_end = Clock::now();

    std::size_t hits = 0U;
    const auto query_begin = Clock::now();
    for (std::size_t index = 0U; index < query_count; ++index) {
        const std::uint64_t start = static_cast<std::uint64_t>(index * 97U) * 3U;
        hits += coordinate_index.query(0U, start, start + 1U).size();
    }
    const auto query_end = Clock::now();

    const auto build_ms = std::chrono::duration_cast<std::chrono::milliseconds>(build_end - build_begin).count();
    const auto query_us = std::chrono::duration_cast<std::chrono::microseconds>(query_end - query_begin).count();

    std::cout << "records=" << record_count << '\n';
    std::cout << "queries=" << query_count << '\n';
    std::cout << "hits=" << hits << '\n';
    std::cout << "index_entries=" << coordinate_index.size() << '\n';
    std::cout << "build_ms=" << build_ms << '\n';
    std::cout << "query_total_us=" << query_us << '\n';

    return hits == query_count && coordinate_index.size() == record_count
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
