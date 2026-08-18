#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>

#include "fastq_statistics.hpp"

namespace biocore::fastq_qc {

inline constexpr const char* default_read1_adapter = "AGATCGGAAGAGCACACGTCTGAACTCCAGTCA";
inline constexpr const char* default_read2_adapter = "AGATCGGAAGAGCGTCGTGTAGGGAAAGAGTGT";

struct TrimmingOptions final {
    std::string adapter_read1{default_read1_adapter};
    std::string adapter_read2{default_read2_adapter};
    std::uint32_t minimum_adapter_overlap{6U};
    std::uint32_t maximum_adapter_mismatches{0U};
    std::uint32_t quality_threshold{20U};
    std::uint64_t minimum_length{20U};
};

struct SingleTrimmingStatistics final {
    std::uint64_t input_reads{0U};
    std::uint64_t kept_reads{0U};
    std::uint64_t discarded_reads{0U};
    std::uint64_t adapter_trimmed_reads{0U};
    std::uint64_t quality_trimmed_reads{0U};
    std::uint64_t adapter_trimmed_bases{0U};
    std::uint64_t quality_trimmed_bases{0U};
    FastqStatistics input;
    FastqStatistics output;
};

struct PairedTrimmingStatistics final {
    std::uint64_t input_pairs{0U};
    std::uint64_t kept_pairs{0U};
    std::uint64_t discarded_pairs{0U};
    std::uint64_t read1_adapter_trimmed_reads{0U};
    std::uint64_t read2_adapter_trimmed_reads{0U};
    std::uint64_t read1_quality_trimmed_reads{0U};
    std::uint64_t read2_quality_trimmed_reads{0U};
    std::uint64_t read1_adapter_trimmed_bases{0U};
    std::uint64_t read2_adapter_trimmed_bases{0U};
    std::uint64_t read1_quality_trimmed_bases{0U};
    std::uint64_t read2_quality_trimmed_bases{0U};
    FastqStatistics input_read1;
    FastqStatistics input_read2;
    FastqStatistics output_read1;
    FastqStatistics output_read2;
};

void validate_trimming_options(const TrimmingOptions& options, bool paired);
[[nodiscard]] SingleTrimmingStatistics trim_single_fastq(
    std::istream& input, std::ostream& output, const TrimmingOptions& options
);
[[nodiscard]] PairedTrimmingStatistics trim_paired_fastq(
    std::istream& read1, std::istream& read2,
    std::ostream& output1, std::ostream& output2,
    const TrimmingOptions& options
);
[[nodiscard]] std::string render_single_trimming_json(
    const SingleTrimmingStatistics& statistics, const TrimmingOptions& options
);
[[nodiscard]] std::string render_single_trimming_tsv(
    const SingleTrimmingStatistics& statistics, const TrimmingOptions& options
);
[[nodiscard]] std::string render_paired_trimming_json(
    const PairedTrimmingStatistics& statistics, const TrimmingOptions& options
);
[[nodiscard]] std::string render_paired_trimming_tsv(
    const PairedTrimmingStatistics& statistics, const TrimmingOptions& options
);

}  // namespace biocore::fastq_qc
