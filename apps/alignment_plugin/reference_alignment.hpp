#pragma once

#include <cstdint>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

#include "alignment_io.hpp"

namespace biocore::alignment {

struct AlignmentOptions final {
    std::uint32_t maximum_mismatches{2U};
};

struct ReadAlignmentStatistics final {
    std::uint64_t total_reads{0U};
    std::uint64_t mapped_reads{0U};
    std::uint64_t unmapped_reads{0U};
    std::uint64_t uniquely_mapped_reads{0U};
    std::uint64_t multi_mapped_reads{0U};
    std::uint64_t reverse_strand_reads{0U};
    std::uint64_t aligned_bases{0U};
    std::uint64_t mismatch_bases{0U};
};

struct PairedAlignmentStatistics final {
    std::uint64_t total_pairs{0U};
    std::uint64_t both_mapped_pairs{0U};
    std::uint64_t single_mapped_pairs{0U};
    std::uint64_t unmapped_pairs{0U};
    ReadAlignmentStatistics reads;
};

void validate_alignment_options(const AlignmentOptions& options);

[[nodiscard]] ReadAlignmentStatistics align_single_fastq(
    const std::vector<ReferenceContig>& reference,
    std::istream& reads,
    std::ostream& sam,
    const AlignmentOptions& options
);

[[nodiscard]] PairedAlignmentStatistics align_paired_fastq(
    const std::vector<ReferenceContig>& reference,
    std::istream& read1,
    std::istream& read2,
    std::ostream& sam,
    const AlignmentOptions& options
);

[[nodiscard]] std::string render_single_alignment_json(
    const ReadAlignmentStatistics& statistics,
    const AlignmentOptions& options
);
[[nodiscard]] std::string render_single_alignment_tsv(
    const ReadAlignmentStatistics& statistics,
    const AlignmentOptions& options
);
[[nodiscard]] std::string render_paired_alignment_json(
    const PairedAlignmentStatistics& statistics,
    const AlignmentOptions& options
);
[[nodiscard]] std::string render_paired_alignment_tsv(
    const PairedAlignmentStatistics& statistics,
    const AlignmentOptions& options
);

}  // namespace biocore::alignment
