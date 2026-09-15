#include "biocore/application/build_info.hpp"
#include "biocore/application/variant_export.hpp"
#include "biocore/presentation/variant_export.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>

namespace {
using namespace biocore;
using namespace biocore::domain;

constexpr std::size_t variant_count = 100000U;
constexpr std::string_view reference_sha =
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

void require(const bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string make_fasta() {
    std::string value{">chr1\n"};
    value.reserve(variant_count + (variant_count / 80U) + 64U);
    for (std::size_t index = 0U; index < variant_count + 1000U; ++index) {
        value.push_back('A');
        if ((index + 1U) % 80U == 0U) value.push_back('\n');
    }
    if (value.back() != '\n') value.push_back('\n');
    return value;
}

std::string make_vcf() {
    std::ostringstream output;
    output << "##fileformat=VCFv4.3\n"
           << "##FORMAT=<ID=GT,Number=1,Type=String,Description=\"Genotype\">\n"
           << "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\tFORMAT\tsample-1\n";
    for (std::size_t index = 0U; index < variant_count; ++index) {
        output << "chr1\t" << (index + 1U) << "\t.\tA\tG\t.\tPASS\t.\tGT\t0/1\n";
    }
    return output.str();
}

class CountingBuffer final : public std::streambuf {
public:
    [[nodiscard]] std::uint64_t bytes() const noexcept { return bytes_; }

protected:
    std::streamsize xsputn(const char*, const std::streamsize count) override {
        if (count > 0) bytes_ += static_cast<std::uint64_t>(count);
        return count;
    }

    int_type overflow(const int_type character) override {
        if (!traits_type::eq_int_type(character, traits_type::eof())) ++bytes_;
        return character;
    }

private:
    std::uint64_t bytes_{0U};
};

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
    require(matrix.variant_count() == variant_count, "export benchmark matrix count mismatch");
    const auto workspace = VariantAnalysisWorkspace::build(matrix);

    application::VariantExportProvenance provenance{
        .schema_version = application::VariantExportProvenance::current_schema_version,
        .producer_name = "OpenGenesis-BioCore",
        .producer_version = std::string{application::BuildInfo::version()},
        .generated_at_utc = "2026-09-15T12:00:00Z",
        .pipeline = {
            .pipeline_id = "org.biocore.variant.workspace",
            .pipeline_version = "0.3.0",
            .job_id = std::nullopt,
            .job_revision = std::nullopt,
            .attempt_number = std::nullopt,
        },
        .reference = {
            .reference_id = "GRCh38-benchmark",
            .assembly = assembly,
            .source_sha256 = std::string{reference_sha},
        },
        .databases = {},
    };

    const auto plan_started = std::chrono::steady_clock::now();
    const auto plan = application::build_variant_export_plan(workspace, std::move(provenance));
    const auto plan_finished = std::chrono::steady_clock::now();
    require(plan.row_count() == variant_count, "export benchmark plan count mismatch");

    CountingBuffer buffer;
    std::ostream sink{&buffer};
    const auto render_started = std::chrono::steady_clock::now();
    presentation::write_variant_export_tsv(sink, plan);
    const auto render_finished = std::chrono::steady_clock::now();
    require(buffer.bytes() > variant_count * 20U, "export benchmark produced unexpectedly little output");

    const auto plan_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        plan_finished - plan_started
    ).count();
    const auto render_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        render_finished - render_started
    ).count();
    std::cout << "{\"variants\":" << variant_count
              << ",\"format\":\"tsv\""
              << ",\"bytes\":" << buffer.bytes()
              << ",\"plan_ms\":" << plan_ms
              << ",\"render_ms\":" << render_ms
              << "}\n";
}
