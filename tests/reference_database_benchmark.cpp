#include "biocore/domain/reference_database.hpp"
#include "biocore/domain/variant_types.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace biocore::domain;

int main() {
    ContigTableBuilder builder{ReferenceAssembly::grch38};
    builder.add_contig("1", {"chr1"});
    const ContigTable contigs = builder.build();
    const ContigId chr1 = *contigs.resolve("1");

    constexpr std::size_t record_count = 100'000U;
    std::vector<ReferenceDatabaseRecord> records;
    records.reserve(record_count);
    for (std::size_t index = 0U; index < record_count; ++index) {
        const std::uint64_t start = static_cast<std::uint64_t>(index) * 2U;
        records.push_back(ReferenceDatabaseRecord{
            .locus = GenomicInterval{chr1, start, start + 1U},
            .reference = Allele{"A", VariantType::unknown, false},
            .alternate = Allele{"G", VariantType::snv, false},
            .record_id = "r" + std::to_string(index),
            .attributes = {}
        });
    }

    ReferenceDatabaseMetadata metadata{
        .database_id = "benchmark",
        .display_name = "Benchmark",
        .version = "1.0",
        .schema_version = 1U,
        .assembly = ReferenceAssemblyIdentity{ReferenceAssembly::grch38, {}},
        .source_uri = "memory://benchmark",
        .source_sha256 = std::string(64U, 'd')
    };

    const auto build_start = std::chrono::steady_clock::now();
    const ReferenceDatabase database = ReferenceDatabase::build(std::move(metadata), contigs, std::move(records));
    const auto build_end = std::chrono::steady_clock::now();

    constexpr std::size_t query_count = 1'000'000U;
    const ReferenceAssemblyIdentity assembly{ReferenceAssembly::grch38, {}};
    std::uint64_t checksum = 0U;
    const auto query_start = std::chrono::steady_clock::now();
    for (std::size_t query = 0U; query < query_count; ++query) {
        const std::size_t selected = query % record_count;
        const std::uint64_t start = static_cast<std::uint64_t>(selected) * 2U;
        const auto hits = database.query_overlap(assembly, contigs, chr1, start, start + 1U);
        if (hits.size() != 1U) {
            return 2;
        }
        checksum += static_cast<std::uint64_t>(hits.front());
    }
    const auto query_end = std::chrono::steady_clock::now();

    const auto build_ms = std::chrono::duration_cast<std::chrono::milliseconds>(build_end - build_start).count();
    const auto query_ms = std::chrono::duration_cast<std::chrono::milliseconds>(query_end - query_start).count();
    std::cout << "records=" << database.size() << '\n';
    std::cout << "queries=" << query_count << '\n';
    std::cout << "hits=" << query_count << '\n';
    std::cout << "checksum=" << checksum << '\n';
    std::cout << "build_ms=" << build_ms << '\n';
    std::cout << "query_ms=" << query_ms << '\n';
}
