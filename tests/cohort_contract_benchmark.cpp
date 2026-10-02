#include "biocore/domain/case_control_association.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace biocore::domain;

int main(const int argc, char** argv) {
    if (argc != 4) return 1;
    const auto samples = static_cast<std::size_t>(std::stoull(argv[1]));
    const auto variants = static_cast<std::size_t>(std::stoull(argv[2]));
    const auto density = static_cast<std::size_t>(std::stoull(argv[3]));
    if (samples < 2U || samples > 100U || samples % 2U != 0U || variants == 0U
        || variants > 10000U || variants % 100U != 0U || (density != 10U && density != 100U)
        || (density == 10U && samples != 100U)) return 1;
    ContigTableBuilder builder{ReferenceAssembly::grch38};
    builder.add_contig("1");
    const auto contigs = builder.build();
    const auto chr = *contigs.resolve("1");
    const ReferenceAssemblyIdentity assembly{ReferenceAssembly::grch38, {}};
    std::vector<VcfIngestionResult> inputs(samples);
    std::vector<MultiSampleMatrixSource> sources;
    std::vector<CaseControlSampleLabel> labels;
    for (std::size_t s = 0; s < samples; ++s) {
        const auto id = "S" + std::to_string(s);
        inputs[s].header.sample_names.push_back(id);
        labels.push_back({id, s < samples / 2U ? CaseControlPhenotype::case_sample : CaseControlPhenotype::control});
        for (std::size_t v = 0; v < variants; ++v) {
            if ((v + s) % 100U >= density) continue;
            CanonicalVariantRecord record;
            record.variant.locus = {chr, v, v + 1U};
            record.variant.reference = {"A", VariantType::unknown, false};
            record.variant.alternates.push_back({"G", VariantType::snv, false});
            record.samples.resize(1U);
            auto& sample = record.samples.front();
            sample.genotype_present = true;
            sample.genotype.allele_indices.push_back(0);
            sample.genotype.allele_indices.push_back(1);
            sample.genotype.phased_separators.push_back(0);
            inputs[s].records.push_back(std::move(record));
        }
        sources.push_back({id, assembly, &contigs, &inputs[s]});
    }
    const auto start = std::chrono::steady_clock::now();
    const auto matrix = build_multi_sample_matrix(assembly, contigs, sources);
    const auto built = std::chrono::steady_clock::now();
    const auto result = analyze_case_control(matrix, labels);
    const auto finished = std::chrono::steady_clock::now();
    std::uint64_t checksum = 0;
    for (const auto& row : result.variants) checksum += row.allele_table.case_exposed + row.allele_table.control_exposed;
    const auto expected = samples * variants * density / 100U;
    if (matrix.sample_count() != samples || matrix.variant_count() != variants
        || matrix.observation_count() != expected || checksum != expected) {
        throw std::runtime_error("benchmark oracle failed");
    }
    std::cout << "samples=" << samples << "\nvariants=" << variants << "\noccupancy_percent=" << density
              << "\nobservations=" << matrix.observation_count() << "\nchecksum=" << checksum
              << "\nbuild_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(built - start).count()
              << "\nassociation_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(finished - built).count()
              << '\n';
}
