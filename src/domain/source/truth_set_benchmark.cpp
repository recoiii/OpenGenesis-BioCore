#include "biocore/domain/truth_set_benchmark.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace biocore::domain {
namespace {

constexpr std::size_t variant_type_count = 6U;

[[nodiscard]] std::size_t type_index(const VariantType type) {
    switch (type) {
        case VariantType::snv: return 0U;
        case VariantType::insertion: return 1U;
        case VariantType::deletion: return 2U;
        case VariantType::mnv: return 3U;
        case VariantType::delins_complex: return 4U;
        case VariantType::unknown: return 5U;
    }
    throw std::invalid_argument("truth-set benchmark variant type is invalid");
}

[[nodiscard]] VariantType type_from_index(const std::size_t index) {
    static constexpr std::array values{
        VariantType::snv,
        VariantType::insertion,
        VariantType::deletion,
        VariantType::mnv,
        VariantType::delins_complex,
        VariantType::unknown,
    };
    return values.at(index);
}

[[nodiscard]] auto identity_tuple(const BenchmarkVariantObservation& value) {
    return std::tie(
        value.contig_name,
        value.start,
        value.end,
        value.reference_allele,
        value.alternate_allele,
        value.symbolic
    );
}

[[nodiscard]] bool identity_less(
    const BenchmarkVariantObservation& left,
    const BenchmarkVariantObservation& right
) {
    return identity_tuple(left) < identity_tuple(right);
}

[[nodiscard]] bool identity_equal(
    const BenchmarkVariantObservation& left,
    const BenchmarkVariantObservation& right
) {
    return identity_tuple(left) == identity_tuple(right);
}

void finalize_variant_counts(VariantBenchmarkCounts& counts) {
    const std::uint64_t predicted_positive = counts.true_positives + counts.false_positives;
    const std::uint64_t actual_positive = counts.true_positives + counts.false_negatives;
    if (predicted_positive != 0U) {
        counts.precision = static_cast<double>(counts.true_positives) /
            static_cast<double>(predicted_positive);
    }
    if (actual_positive != 0U) {
        counts.recall = static_cast<double>(counts.true_positives) /
            static_cast<double>(actual_positive);
    }
    if (counts.precision.has_value() && counts.recall.has_value()) {
        const double denominator = *counts.precision + *counts.recall;
        counts.f1 = denominator == 0.0
            ? 0.0
            : (2.0 * *counts.precision * *counts.recall) / denominator;
    }
}

void finalize_genotypes(GenotypeConcordanceCounts& counts) {
    counts.discordant = counts.comparable - counts.concordant;
    if (counts.comparable != 0U) {
        counts.concordance = static_cast<double>(counts.concordant) /
            static_cast<double>(counts.comparable);
    }
}

[[nodiscard]] std::optional<BenchmarkGenotype> benchmark_genotype(
    const CanonicalVariantRecord& record,
    const std::size_t sample_index
) {
    if (record.variant.alternates.size() != 1U || sample_index >= record.samples.size()) {
        return std::nullopt;
    }
    const SampleVariantData& sample = record.samples[sample_index];
    if (!sample.genotype_present || sample.genotype.allele_indices.empty()) {
        return std::nullopt;
    }
    std::uint32_t alternate_dosage = 0U;
    for (const std::int16_t allele : sample.genotype.allele_indices.values()) {
        if (allele == missing_allele_index) {
            return std::nullopt;
        }
        if (allele != 0 && allele != 1) {
            return std::nullopt;
        }
        if (allele == 1) {
            ++alternate_dosage;
        }
    }
    if (sample.genotype.allele_indices.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw std::overflow_error("truth-set benchmark genotype ploidy exceeds uint32 range");
    }
    return BenchmarkGenotype{
        .ploidy = static_cast<std::uint32_t>(sample.genotype.allele_indices.size()),
        .alternate_dosage = alternate_dosage,
    };
}

[[nodiscard]] std::optional<std::size_t> selected_sample_index(
    const VcfIngestionResult& input,
    const VcfBenchmarkAdapterOptions& options
) {
    if (!options.sample_id.has_value()) {
        return std::nullopt;
    }
    const auto found = std::find(
        input.header.sample_names.begin(),
        input.header.sample_names.end(),
        *options.sample_id
    );
    if (found == input.header.sample_names.end()) {
        throw std::invalid_argument("truth-set benchmark sample id is not present in VCF header");
    }
    return static_cast<std::size_t>(std::distance(input.header.sample_names.begin(), found));
}

[[nodiscard]] bool record_passes(const CanonicalVariantRecord& record) noexcept {
    return record.filters_applied && record.filters.empty();
}

void require_unique_sorted(
    const std::span<const BenchmarkVariantObservation> observations,
    const std::vector<std::size_t>& order,
    const char* label
) {
    for (std::size_t index = 1U; index < order.size(); ++index) {
        if (identity_equal(observations[order[index - 1U]], observations[order[index]])) {
            throw std::invalid_argument(std::string{"truth-set benchmark contains duplicate "} + label + " variant identity");
        }
    }
}

[[nodiscard]] std::vector<std::size_t> sorted_order(
    const std::span<const BenchmarkVariantObservation> observations,
    const char* label
) {
    std::vector<std::size_t> order(observations.size());
    std::iota(order.begin(), order.end(), 0U);
    for (const auto& observation : observations) {
        validate_benchmark_observation(observation);
    }
    std::ranges::sort(order, [&](const std::size_t left, const std::size_t right) {
        return identity_less(observations[left], observations[right]);
    });
    require_unique_sorted(observations, order, label);
    return order;
}

}  // namespace

void validate_benchmark_observation(const BenchmarkVariantObservation& observation) {
    if (observation.contig_name.empty() || observation.contig_name.size() > 1024U ||
        observation.contig_name.find('\0') != std::string::npos) {
        throw std::invalid_argument("truth-set benchmark contig name is invalid");
    }
    if (observation.start >= observation.end) {
        throw std::invalid_argument("truth-set benchmark interval must be non-empty");
    }
    if (observation.reference_allele.empty() || observation.alternate_allele.empty()) {
        throw std::invalid_argument("truth-set benchmark alleles must not be empty");
    }
    if (observation.reference_allele == observation.alternate_allele) {
        throw std::invalid_argument("truth-set benchmark reference and alternate alleles must differ");
    }
    static_cast<void>(type_index(observation.variant_type));
    const VariantType classified = classify_variant(
        observation.reference_allele,
        observation.alternate_allele,
        observation.symbolic
    );
    if (classified != observation.variant_type) {
        throw std::invalid_argument("truth-set benchmark variant type does not match allele representation");
    }
    if (observation.genotype.has_value()) {
        if (observation.genotype->ploidy == 0U ||
            observation.genotype->alternate_dosage > observation.genotype->ploidy) {
            throw std::invalid_argument("truth-set benchmark genotype dosage is invalid");
        }
    }
}

std::vector<BenchmarkVariantObservation> benchmark_observations_from_vcf(
    const VcfIngestionResult& input,
    const ContigTable& contigs,
    const VcfBenchmarkAdapterOptions& options
) {
    const auto sample_index = selected_sample_index(input, options);
    std::vector<BenchmarkVariantObservation> result;
    std::size_t alternate_count = 0U;
    for (const auto& record : input.records) {
        if (options.pass_only && !record_passes(record)) {
            continue;
        }
        alternate_count += record.variant.alternates.size();
    }
    result.reserve(alternate_count);

    for (const auto& record : input.records) {
        if (options.pass_only && !record_passes(record)) {
            continue;
        }
        const auto contig_name = contigs.canonical_name(record.variant.locus.contig_id);
        if (!contig_name.has_value()) {
            throw std::invalid_argument("truth-set benchmark VCF record references an unknown contig");
        }
        for (std::size_t alternate_index = 0U;
             alternate_index < record.variant.alternates.size();
             ++alternate_index) {
            const Allele& alternate = record.variant.alternates[alternate_index];
            BenchmarkVariantObservation observation{
                .contig_name = std::string{*contig_name},
                .start = record.variant.locus.start,
                .end = record.variant.locus.end,
                .reference_allele = record.variant.reference.sequence,
                .alternate_allele = alternate.sequence,
                .variant_type = alternate.type,
                .symbolic = alternate.symbolic,
                .genotype = std::nullopt,
            };
            if (sample_index.has_value() && record.variant.alternates.size() == 1U) {
                observation.genotype = benchmark_genotype(record, *sample_index);
            }
            validate_benchmark_observation(observation);
            result.push_back(std::move(observation));
        }
    }
    return result;
}

TruthSetBenchmarkResult benchmark_truth_set(
    const std::span<const BenchmarkVariantObservation> truth,
    const std::span<const BenchmarkVariantObservation> candidate
) {
    const auto truth_order = sorted_order(truth, "truth");
    const auto candidate_order = sorted_order(candidate, "candidate");

    TruthSetBenchmarkResult result;
    std::array<VariantBenchmarkCounts, variant_type_count> stratified_variants{};
    std::array<GenotypeConcordanceCounts, variant_type_count> stratified_genotypes{};

    std::size_t truth_cursor = 0U;
    std::size_t candidate_cursor = 0U;
    while (truth_cursor < truth_order.size() || candidate_cursor < candidate_order.size()) {
        if (truth_cursor == truth_order.size()) {
            const auto& query = candidate[candidate_order[candidate_cursor++]];
            ++result.overall.candidate_total;
            ++result.overall.false_positives;
            auto& counts = stratified_variants[type_index(query.variant_type)];
            ++counts.candidate_total;
            ++counts.false_positives;
            continue;
        }
        if (candidate_cursor == candidate_order.size()) {
            const auto& expected = truth[truth_order[truth_cursor++]];
            ++result.overall.truth_total;
            ++result.overall.false_negatives;
            auto& counts = stratified_variants[type_index(expected.variant_type)];
            ++counts.truth_total;
            ++counts.false_negatives;
            continue;
        }

        const auto& expected = truth[truth_order[truth_cursor]];
        const auto& query = candidate[candidate_order[candidate_cursor]];
        if (identity_less(expected, query)) {
            ++truth_cursor;
            ++result.overall.truth_total;
            ++result.overall.false_negatives;
            auto& counts = stratified_variants[type_index(expected.variant_type)];
            ++counts.truth_total;
            ++counts.false_negatives;
            continue;
        }
        if (identity_less(query, expected)) {
            ++candidate_cursor;
            ++result.overall.candidate_total;
            ++result.overall.false_positives;
            auto& counts = stratified_variants[type_index(query.variant_type)];
            ++counts.candidate_total;
            ++counts.false_positives;
            continue;
        }

        ++truth_cursor;
        ++candidate_cursor;
        ++result.overall.truth_total;
        ++result.overall.candidate_total;
        ++result.overall.true_positives;
        auto& counts = stratified_variants[type_index(expected.variant_type)];
        ++counts.truth_total;
        ++counts.candidate_total;
        ++counts.true_positives;

        if (expected.variant_type != query.variant_type) {
            throw std::logic_error("truth-set benchmark identity matched with inconsistent variant types");
        }
        if (expected.genotype.has_value() && query.genotype.has_value()) {
            ++result.genotypes.comparable;
            auto& genotype_counts = stratified_genotypes[type_index(expected.variant_type)];
            ++genotype_counts.comparable;
            if (*expected.genotype == *query.genotype) {
                ++result.genotypes.concordant;
                ++genotype_counts.concordant;
            }
        }
    }

    finalize_variant_counts(result.overall);
    finalize_genotypes(result.genotypes);
    result.by_type.reserve(variant_type_count);
    for (std::size_t index = 0U; index < variant_type_count; ++index) {
        finalize_variant_counts(stratified_variants[index]);
        finalize_genotypes(stratified_genotypes[index]);
        result.by_type.push_back(VariantBenchmarkStratum{
            .variant_type = type_from_index(index),
            .variants = stratified_variants[index],
            .genotypes = stratified_genotypes[index],
        });
    }
    return result;
}

}  // namespace biocore::domain
