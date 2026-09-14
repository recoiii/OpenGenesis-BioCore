#include "biocore/domain/variant_record.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    using biocore::domain::Allele;
    using biocore::domain::VariantRecord;
    using biocore::domain::VariantType;

    constexpr std::size_t record_count = 1'000'000U;
    std::vector<VariantRecord> records;
    records.reserve(record_count);

    const auto start = std::chrono::steady_clock::now();
    for (std::size_t index = 0U; index < record_count; ++index) {
        VariantRecord record;
        const auto position = static_cast<std::uint64_t>(index);
        record.locus = {0U, position, position + 1U};
        record.reference = Allele{"A", VariantType::unknown, false};
        record.alternates.push_back(Allele{"G", VariantType::snv, false});
        records.push_back(std::move(record));
    }
    const auto build_end = std::chrono::steady_clock::now();

    std::uint64_t checksum = 0U;
    std::size_t heap_alt_records = 0U;
    for (const VariantRecord& record : records) {
        checksum += record.locus.start;
        if (record.alternates.uses_heap_storage()) {
            ++heap_alt_records;
        }
    }
    const auto traversal_end = std::chrono::steady_clock::now();

    const auto build_ms = std::chrono::duration_cast<std::chrono::milliseconds>(build_end - start).count();
    const auto traversal_ms = std::chrono::duration_cast<std::chrono::milliseconds>(traversal_end - build_end).count();
    std::cout << "records=" << records.size() << '\n';
    std::cout << "sizeof_variant_record=" << sizeof(VariantRecord) << '\n';
    std::cout << "nominal_contiguous_bytes=" << (sizeof(VariantRecord) * records.size()) << '\n';
    std::cout << "common_path_alt_heap_records=" << heap_alt_records << '\n';
    std::cout << "construction_ms=" << build_ms << '\n';
    std::cout << "traversal_ms=" << traversal_ms << '\n';
    std::cout << "checksum=" << checksum << '\n';
    return heap_alt_records == 0U ? 0 : 1;
}
