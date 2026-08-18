#pragma once

#include <cstdint>
#include <istream>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace biocore::vcf_qc {

struct VcfQcOptions final {
    bool depth_filter_enabled{true};
    bool alt_count_filter_enabled{true};
    bool alt_fraction_filter_enabled{true};
    bool alt_base_quality_filter_enabled{true};
    std::uint64_t minimum_depth{3U};
    std::uint64_t minimum_alt_count{2U};
    double minimum_alt_fraction{0.20};
    double minimum_alt_base_quality{20.0};
};

struct VariantQcRow final {
    std::string variant_key;
    std::string chrom;
    std::uint64_t position{0U};
    std::string id;
    std::string reference;
    std::string alternate;
    std::uint64_t allele_index{0U};
    std::string variant_type;
    std::optional<double> qual;
    std::string input_filter;
    std::string output_filter;
    std::optional<std::uint64_t> depth;
    std::optional<std::uint64_t> alt_count;
    std::optional<double> alt_fraction;
    std::optional<double> alt_base_quality;
    bool passed{false};
};

struct VcfQcStatistics final {
    std::string file_format;
    std::uint64_t total_records{0U};
    std::uint64_t total_alt_alleles{0U};
    std::uint64_t passed_records{0U};
    std::uint64_t filtered_records{0U};
    std::uint64_t passed_alt_alleles{0U};
    std::uint64_t filtered_alt_alleles{0U};
    std::uint64_t snv_alleles{0U};
    std::uint64_t mnv_alleles{0U};
    std::uint64_t insertion_like_alleles{0U};
    std::uint64_t deletion_like_alleles{0U};
    std::uint64_t symbolic_alleles{0U};
    std::uint64_t complex_alleles{0U};
    std::uint64_t transitions{0U};
    std::uint64_t transversions{0U};
    std::uint64_t unfiltered_dot_records{0U};
    std::uint64_t prefiltered_records{0U};
    std::uint64_t alleles_missing_depth{0U};
    std::uint64_t alleles_missing_alt_count{0U};
    std::uint64_t alleles_missing_alt_fraction{0U};
    std::uint64_t alleles_missing_alt_base_quality{0U};
    std::map<std::string, std::uint64_t> filter_reason_counts;
    std::map<std::string, std::uint64_t> contig_record_counts;
    std::vector<VariantQcRow> rows;
};

struct VcfQcResult final {
    VcfQcStatistics statistics;
    std::string filtered_vcf;
};

void validate_vcf_qc_options(const VcfQcOptions& options);
[[nodiscard]] VcfQcResult process_vcf(std::istream& input, const VcfQcOptions& options);
[[nodiscard]] std::string render_vcf_qc_json(const VcfQcStatistics& statistics, const VcfQcOptions& options);
[[nodiscard]] std::string render_vcf_qc_tsv(const VcfQcStatistics& statistics);

}  // namespace biocore::vcf_qc
