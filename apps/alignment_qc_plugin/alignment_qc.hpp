#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <limits>
#include <map>
#include <string>
#include <string_view>

namespace biocore::alignment_qc {

struct ContigCoverageStatistics final {
    std::uint64_t reference_length{0};
    std::uint64_t covered_bases{0};
    std::uint64_t total_depth{0};
    std::uint64_t maximum_depth{0};
    std::uint64_t bases_at_least_1x{0};
    std::uint64_t bases_at_least_10x{0};
    std::uint64_t bases_at_least_20x{0};
    std::uint64_t bases_at_least_30x{0};
};

struct AlignmentQcStatistics final {
    std::string input_format;
    std::uint64_t total_records{0};
    std::uint64_t primary_records{0};
    std::uint64_t primary_mapped{0};
    std::uint64_t primary_unmapped{0};
    std::uint64_t secondary_records{0};
    std::uint64_t supplementary_records{0};
    std::uint64_t duplicate_records{0};
    std::uint64_t qc_failed_records{0};
    std::uint64_t paired_primary_records{0};
    std::uint64_t proper_pair_primary_records{0};
    std::uint64_t read1_primary_records{0};
    std::uint64_t read2_primary_records{0};
    std::uint64_t reverse_primary_mapped{0};
    std::uint64_t mate_unmapped_primary_records{0};
    std::uint64_t mapq_observations{0};
    std::uint64_t mapq_sum{0};
    std::uint32_t minimum_mapq{std::numeric_limits<std::uint32_t>::max()};
    std::uint32_t maximum_mapq{0};
    std::array<std::uint64_t, 7> mapq_bins{};
    std::uint64_t nm_observations{0};
    std::uint64_t nm_sum{0};
    std::uint64_t minimum_nm{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t maximum_nm{0};
    std::map<std::string, std::uint64_t> contig_primary_mapped;

    bool coverage_available{false};
    std::uint64_t total_reference_bases{0};
    std::uint64_t covered_reference_bases{0};
    std::uint64_t total_depth{0};
    std::uint64_t maximum_depth{0};
    std::uint64_t bases_at_least_1x{0};
    std::uint64_t bases_at_least_10x{0};
    std::uint64_t bases_at_least_20x{0};
    std::uint64_t bases_at_least_30x{0};
    std::map<std::string, ContigCoverageStatistics> contig_coverage;

    std::uint64_t template_length_observations{0};
    std::uint64_t template_length_sum{0};
    std::uint64_t minimum_template_length{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t maximum_template_length{0};
};

[[nodiscard]] AlignmentQcStatistics analyze_sam(std::istream& input);
[[nodiscard]] AlignmentQcStatistics analyze_bam(std::istream& decompressed_input);
[[nodiscard]] AlignmentQcStatistics analyze_alignment_file(
    const std::filesystem::path& path, std::string_view file_type
);
[[nodiscard]] std::string render_alignment_qc_json(const AlignmentQcStatistics& statistics);
[[nodiscard]] std::string render_alignment_qc_tsv(const AlignmentQcStatistics& statistics);

}  // namespace biocore::alignment_qc
