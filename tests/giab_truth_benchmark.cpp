#include "biocore/domain/truth_set_benchmark.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace biocore::domain;

struct BedInterval final {
    std::string contig;
    std::uint64_t start{0U};
    std::uint64_t end{0U};
};

std::vector<std::string_view> split(const std::string_view value, const char delimiter) {
    std::vector<std::string_view> result;
    std::size_t start = 0U;
    for (;;) {
        const auto end = value.find(delimiter, start);
        result.push_back(value.substr(start, end == std::string_view::npos ? value.size() - start : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1U;
    }
    return result;
}

std::uint64_t parse_u64(const std::string_view value, const char* field) {
    if (value.empty()) throw std::invalid_argument(std::string{field} + " is empty");
    std::uint64_t result = 0U;
    for (const char c : value) {
        if (c < '0' || c > '9') throw std::invalid_argument(std::string{field} + " is not numeric");
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (result > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) throw std::overflow_error(std::string{field} + " overflows");
        result = result * 10U + digit;
    }
    return result;
}

bool symbolic_alt(const std::string_view alt) noexcept {
    return alt == "*" || (alt.size() >= 2U && alt.front() == '<' && alt.back() == '>');
}

std::vector<BedInterval> load_bed(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open GIAB confidence BED");
    std::vector<BedInterval> result;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        const auto fields = split(line, '\t');
        if (fields.size() < 3U) throw std::invalid_argument("GIAB BED record has fewer than three columns");
        const auto start = parse_u64(fields[1], "BED start");
        const auto end = parse_u64(fields[2], "BED end");
        if (start >= end) throw std::invalid_argument("GIAB BED interval is empty");
        result.push_back({std::string{fields[0]}, start, end});
    }
    std::ranges::sort(result, [](const auto& left, const auto& right) {
        if (left.contig != right.contig) return left.contig < right.contig;
        if (left.start != right.start) return left.start < right.start;
        return left.end < right.end;
    });
    return result;
}

bool inside_confident_region(
    const std::vector<BedInterval>& regions,
    const std::string_view contig,
    const std::uint64_t start,
    const std::uint64_t end
) {
    const auto contig_begin = std::lower_bound(
        regions.begin(), regions.end(), contig,
        [](const BedInterval& interval, const std::string_view name) {
            return interval.contig < name;
        }
    );
    const auto contig_end = std::upper_bound(
        contig_begin, regions.end(), contig,
        [](const std::string_view name, const BedInterval& interval) {
            return name < interval.contig;
        }
    );
    if (contig_begin == contig_end) return false;
    const auto after_start = std::upper_bound(
        contig_begin, contig_end, start,
        [](const std::uint64_t coordinate, const BedInterval& interval) {
            return coordinate < interval.start;
        }
    );
    if (after_start == contig_begin) return false;
    const auto& interval = *std::prev(after_start);
    return interval.start <= start && end <= interval.end;
}

std::optional<BenchmarkGenotype> parse_biallelic_gt(
    const std::vector<std::string_view>& fields,
    const std::size_t sample_column
) {
    if (fields.size() <= sample_column || fields.size() <= 8U) return std::nullopt;
    const auto format_keys = split(fields[8], ':');
    const auto gt_it = std::find(format_keys.begin(), format_keys.end(), std::string_view{"GT"});
    if (gt_it == format_keys.end()) return std::nullopt;
    const auto sample_values = split(fields[sample_column], ':');
    const auto gt_index = static_cast<std::size_t>(std::distance(format_keys.begin(), gt_it));
    if (gt_index >= sample_values.size()) return std::nullopt;
    const std::string_view gt = sample_values[gt_index];
    if (gt.empty() || gt == ".") return std::nullopt;

    std::uint32_t ploidy = 0U;
    std::uint32_t alternate_dosage = 0U;
    std::size_t start = 0U;
    while (start <= gt.size()) {
        const auto sep = gt.find_first_of("/|", start);
        const auto token = gt.substr(start, sep == std::string_view::npos ? gt.size() - start : sep - start);
        if (token.empty() || token == ".") return std::nullopt;
        const auto allele = parse_u64(token, "GT allele");
        if (allele > 1U) return std::nullopt;
        ++ploidy;
        if (allele == 1U) ++alternate_dosage;
        if (sep == std::string_view::npos) break;
        start = sep + 1U;
    }
    if (ploidy == 0U) return std::nullopt;
    return BenchmarkGenotype{ploidy, alternate_dosage};
}

std::vector<BenchmarkVariantObservation> load_giab_vcf(
    const std::string& path,
    const std::vector<BedInterval>& regions
) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open GIAB truth VCF");
    std::vector<BenchmarkVariantObservation> result;
    std::string line;
    std::optional<std::size_t> hg002_column;
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        if (line.starts_with("##")) continue;
        if (line.starts_with("#CHROM\t")) {
            const auto columns = split(line, '\t');
            for (std::size_t i = 9U; i < columns.size(); ++i) {
                if (columns[i] == "HG002") {
                    hg002_column = i;
                    break;
                }
            }
            if (!hg002_column.has_value() && columns.size() > 9U) hg002_column = 9U;
            continue;
        }
        if (line.front() == '#') continue;
        const auto fields = split(line, '\t');
        if (fields.size() < 8U) throw std::invalid_argument("GIAB VCF record has fewer than eight columns");
        if (fields[6] != "PASS" && fields[6] != ".") continue;
        const auto position = parse_u64(fields[1], "VCF POS");
        if (position == 0U) throw std::invalid_argument("VCF POS is zero");
        const std::uint64_t start = position - 1U;
        const std::string ref{fields[3]};
        const std::uint64_t end = start + ref.size();
        if (!inside_confident_region(regions, fields[0], start, end)) continue;
        const auto alternates = split(fields[4], ',');
        const bool biallelic = alternates.size() == 1U;
        for (const auto alt_view : alternates) {
            if (alt_view.empty() || alt_view == ".") continue;
            const bool symbolic = symbolic_alt(alt_view);
            std::string alt{alt_view};
            const auto type = classify_variant(ref, alt, symbolic);
            BenchmarkVariantObservation observation{
                .contig_name = std::string{fields[0]},
                .start = start,
                .end = end,
                .reference_allele = ref,
                .alternate_allele = std::move(alt),
                .variant_type = type,
                .symbolic = symbolic,
                .genotype = std::nullopt,
            };
            if (biallelic && hg002_column.has_value()) {
                observation.genotype = parse_biallelic_gt(fields, *hg002_column);
            }
            validate_benchmark_observation(observation);
            result.push_back(std::move(observation));
        }
    }
    return result;
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        if (argc != 3) {
            throw std::invalid_argument("usage: biocore-giab-truth-benchmark <truth.vcf> <confidence.bed>");
        }
        const auto regions = load_bed(argv[2]);
        const auto truth = load_giab_vcf(argv[1], regions);
        if (truth.size() < 1000U) throw std::runtime_error("GIAB fixture unexpectedly contains fewer than 1000 benchmark alleles");
        const auto result = benchmark_truth_set(truth, truth);
        if (result.overall.true_positives != truth.size() || result.overall.false_positives != 0U ||
            result.overall.false_negatives != 0U || !result.overall.precision.has_value() ||
            !result.overall.recall.has_value() || !result.overall.f1.has_value() ||
            *result.overall.precision != 1.0 || *result.overall.recall != 1.0 || *result.overall.f1 != 1.0) {
            throw std::runtime_error("GIAB external truth self-benchmark is not exact");
        }
        std::uint64_t snv = 0U;
        std::uint64_t insertion = 0U;
        std::uint64_t deletion = 0U;
        std::uint64_t complex = 0U;
        for (const auto& stratum : result.by_type) {
            if (stratum.variant_type == VariantType::snv) snv = stratum.variants.truth_total;
            else if (stratum.variant_type == VariantType::insertion) insertion = stratum.variants.truth_total;
            else if (stratum.variant_type == VariantType::deletion) deletion = stratum.variants.truth_total;
            else if (stratum.variant_type == VariantType::mnv || stratum.variant_type == VariantType::delins_complex) complex += stratum.variants.truth_total;
        }
        if (snv == 0U || insertion == 0U || deletion == 0U) {
            throw std::runtime_error("GIAB fixture does not exercise SNV, insertion and deletion strata");
        }
        std::cout
            << "{\"dataset\":\"GIAB HG002 GRCh38 v4.2.1 chr20\""
            << ",\"truth_alleles\":" << truth.size()
            << ",\"snv\":" << snv
            << ",\"insertion\":" << insertion
            << ",\"deletion\":" << deletion
            << ",\"mnv_or_complex\":" << complex
            << ",\"tp\":" << result.overall.true_positives
            << ",\"fp\":" << result.overall.false_positives
            << ",\"fn\":" << result.overall.false_negatives
            << ",\"precision\":1,\"recall\":1,\"f1\":1"
            << ",\"genotype_comparable\":" << result.genotypes.comparable
            << ",\"genotype_concordant\":" << result.genotypes.concordant
            << "}\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "GIAB truth benchmark failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
