#include "biocore/domain/variant_workspace.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
using namespace biocore::domain;

constexpr std::size_t variant_count = 100000U;
constexpr std::size_t query_count = 200U;
constexpr std::size_t requested_window = 100U;

void require(const bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string make_fasta() {
    std::string value{">chr1\n"};
    value.reserve(variant_count + (variant_count / 80U) + 64U);
    for (std::size_t index = 0U; index < variant_count + 1000U; ++index) {
        value.push_back('A');
        if ((index + 1U) % 80U == 0U) {
            value.push_back('\n');
        }
    }
    if (value.back() != '\n') {
        value.push_back('\n');
    }
    return value;
}

std::string make_vcf() {
    std::ostringstream output;
    output
        << "##fileformat=VCFv4.3\n"
        << "##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">\n"
        << "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tsample-1\n";
    for (std::size_t index = 0U; index < variant_count; ++index) {
        output << "chr1\t" << (index + 1U) << "\t.\tA\tG\t.\tPASS\t.\tGT\t0/1\n";
    }
    return output.str();
}

}  // namespace

int main() {
    std::istringstream fasta{make_fasta()};
    const auto genome = ReferenceGenome::from_fasta(fasta, ReferenceAssembly::grch38);
    std::istringstream vcf{make_vcf()};
    const auto variants = ingest_vcf(vcf, genome);

    const ReferenceAssemblyIdentity assembly{ReferenceAssembly::grch38, {}};
    ContigTableBuilder table_builder{ReferenceAssembly::grch38};
    table_builder.add_contig("1", {"chr1"});
    const auto matrix = build_multi_sample_matrix(
        assembly,
        table_builder.build(),
        {{"benchmark", assembly, &genome.contigs(), &variants}}
    );
    require(matrix.variant_count() == variant_count, "benchmark matrix variant count mismatch");

    const auto build_started = std::chrono::steady_clock::now();
    const auto workspace = VariantAnalysisWorkspace::build(matrix);
    const auto build_finished = std::chrono::steady_clock::now();
    const auto contig = matrix.contigs().resolve("chr1");
    require(contig.has_value(), "benchmark contig resolution failed");

    std::uint64_t checksum = 0U;
    std::size_t maximum_rows_returned = 0U;
    const auto query_started = std::chrono::steady_clock::now();
    for (std::size_t iteration = 0U; iteration < query_count; ++iteration) {
        const std::uint64_t start = static_cast<std::uint64_t>((iteration * 499U) % (variant_count - requested_window));
        VariantWorkspaceQuery query;
        query.contig_id = *contig;
        query.start = start;
        query.end = start + requested_window;
        query.limit = requested_window;
        query.query_generation_id = static_cast<std::uint64_t>(iteration + 1U);
        const auto window = workspace.query(query);
        require(window.rows.size() <= requested_window, "workspace benchmark exceeded bounded response window");
        require(window.query_generation_id == query.query_generation_id, "workspace benchmark generation echo mismatch");
        maximum_rows_returned = std::max(maximum_rows_returned, window.rows.size());
        for (const auto& row : window.rows) {
            checksum += static_cast<std::uint64_t>(row.variant_index + 1U);
        }
    }
    const auto query_finished = std::chrono::steady_clock::now();

    require(maximum_rows_returned == requested_window, "workspace benchmark did not exercise a full bounded response window");
    require(checksum != 0U, "workspace benchmark checksum is unexpectedly zero");

    const auto build_ms = std::chrono::duration_cast<std::chrono::milliseconds>(build_finished - build_started).count();
    const auto query_ms = std::chrono::duration_cast<std::chrono::milliseconds>(query_finished - query_started).count();
    std::cout
        << "{\"variants\":" << variant_count
        << ",\"queries\":" << query_count
        << ",\"window_limit\":" << requested_window
        << ",\"max_rows_returned\":" << maximum_rows_returned
        << ",\"workspace_build_ms\":" << build_ms
        << ",\"query_total_ms\":" << query_ms
        << ",\"checksum\":" << checksum
        << "}\n";
}
