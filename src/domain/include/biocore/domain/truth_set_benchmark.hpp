#pragma once

#include "biocore/domain/contig_table.hpp"
#include "biocore/domain/variant_types.hpp"
#include "biocore/domain/vcf_ingestion.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace biocore::domain {

struct BenchmarkGenotype final {
    std::uint32_t ploidy{0U};
    std::uint32_t alternate_dosage{0U};

    friend bool operator==(const BenchmarkGenotype&, const BenchmarkGenotype&) = default;
};

struct BenchmarkVariantObservation final {
    std::string contig_name;
    std::uint64_t start{0U};
    std::uint64_t end{0U};
    std::string reference_allele;
    std::string alternate_allele;
    VariantType variant_type{VariantType::unknown};
    bool symbolic{false};
    std::optional<BenchmarkGenotype> genotype;
};

struct VariantBenchmarkCounts final {
    std::uint64_t truth_total{0U};
    std::uint64_t candidate_total{0U};
    std::uint64_t true_positives{0U};
    std::uint64_t false_positives{0U};
    std::uint64_t false_negatives{0U};
    std::optional<double> precision;
    std::optional<double> recall;
    std::optional<double> f1;
};

struct GenotypeConcordanceCounts final {
    std::uint64_t comparable{0U};
    std::uint64_t concordant{0U};
    std::uint64_t discordant{0U};
    std::optional<double> concordance;
};

struct VariantBenchmarkStratum final {
    VariantType variant_type{VariantType::unknown};
    VariantBenchmarkCounts variants;
    GenotypeConcordanceCounts genotypes;
};

struct TruthSetBenchmarkResult final {
    VariantBenchmarkCounts overall;
    GenotypeConcordanceCounts genotypes;
    std::vector<VariantBenchmarkStratum> by_type;
};

struct VcfBenchmarkAdapterOptions final {
    std::optional<std::string> sample_id;
    bool pass_only{false};
};

void validate_benchmark_observation(const BenchmarkVariantObservation& observation);

[[nodiscard]] std::vector<BenchmarkVariantObservation> benchmark_observations_from_vcf(
    const VcfIngestionResult& input,
    const ContigTable& contigs,
    const VcfBenchmarkAdapterOptions& options = {}
);

[[nodiscard]] TruthSetBenchmarkResult benchmark_truth_set(
    std::span<const BenchmarkVariantObservation> truth,
    std::span<const BenchmarkVariantObservation> candidate
);

}  // namespace biocore::domain
