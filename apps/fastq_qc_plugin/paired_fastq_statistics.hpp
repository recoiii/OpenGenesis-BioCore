#pragma once

#include <cstdint>
#include <istream>
#include <string>

#include "fastq_statistics.hpp"

namespace biocore::fastq_qc {

void validate_paired_fastq_records(const FastqRecord& read1, const FastqRecord& read2);

struct PairedFastqStatistics final {
    std::uint64_t pair_count{0U};
    FastqStatistics read1;
    FastqStatistics read2;
    FastqStatistics combined;
};

[[nodiscard]] PairedFastqStatistics analyze_paired_fastq(
    std::istream& read1, std::istream& read2
);
[[nodiscard]] std::string render_paired_summary_json(
    const PairedFastqStatistics& statistics
);
[[nodiscard]] std::string render_paired_summary_tsv(
    const PairedFastqStatistics& statistics
);

}  // namespace biocore::fastq_qc
