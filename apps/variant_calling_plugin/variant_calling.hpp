#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace biocore::variant_calling {

struct ReferenceContig final {
    std::string name;
    std::string sequence;
};

struct VariantCallingOptions final {
    std::uint32_t minimum_depth{3U};
    std::uint32_t minimum_alt_count{2U};
    double minimum_alt_fraction{0.20};
    std::uint32_t minimum_mapq{20U};
    std::uint32_t minimum_base_quality{20U};
};

struct VariantAllele final {
    char base{'N'};
    std::uint64_t count{0U};
    double fraction{0.0};
    double average_base_quality{0.0};
};

struct VariantSite final {
    std::string contig;
    std::uint64_t position{0U};  // 1-based
    char reference{'N'};
    std::uint64_t depth{0U};
    std::vector<VariantAllele> alternates;
};

struct VariantCallingStatistics final {
    std::string alignment_format;
    std::uint64_t total_records{0U};
    std::uint64_t eligible_records{0U};
    std::uint64_t unmapped_records{0U};
    std::uint64_t secondary_records{0U};
    std::uint64_t supplementary_records{0U};
    std::uint64_t duplicate_records{0U};
    std::uint64_t qc_failed_records{0U};
    std::uint64_t low_mapq_records{0U};
    std::uint64_t unavailable_mapq_records{0U};
    std::uint64_t callable_base_observations{0U};
    std::uint64_t low_quality_base_observations{0U};
    std::uint64_t ambiguous_read_base_observations{0U};
    std::uint64_t noncanonical_reference_observations{0U};
    std::uint64_t sites_with_observations{0U};
    std::uint64_t sites_meeting_minimum_depth{0U};
    std::uint64_t called_sites{0U};
    std::uint64_t called_alt_alleles{0U};
    std::vector<VariantSite> variants;
};

void validate_variant_calling_options(const VariantCallingOptions& options);
[[nodiscard]] std::vector<ReferenceContig> read_reference_fasta(const std::filesystem::path& path);
[[nodiscard]] VariantCallingStatistics call_variants_from_sam(
    std::istream& input,
    const std::vector<ReferenceContig>& reference,
    const VariantCallingOptions& options
);
[[nodiscard]] VariantCallingStatistics call_variants_from_bam(
    std::istream& decompressed_input,
    const std::vector<ReferenceContig>& reference,
    const VariantCallingOptions& options
);
[[nodiscard]] VariantCallingStatistics call_variants_from_alignment_file(
    const std::filesystem::path& path,
    std::string_view file_type,
    const std::vector<ReferenceContig>& reference,
    const VariantCallingOptions& options
);
[[nodiscard]] std::string render_vcf(
    const VariantCallingStatistics& statistics,
    const std::vector<ReferenceContig>& reference,
    const VariantCallingOptions& options
);
[[nodiscard]] std::string render_variant_json(
    const VariantCallingStatistics& statistics,
    const VariantCallingOptions& options
);
[[nodiscard]] std::string render_variant_tsv(const VariantCallingStatistics& statistics);

}  // namespace biocore::variant_calling
