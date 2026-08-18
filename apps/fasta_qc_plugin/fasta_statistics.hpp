#pragma once

#include <cstdint>
#include <istream>
#include <string>

namespace biocore::fasta_qc {

struct FastaStatistics final {
    std::uint64_t sequence_count{0U};
    std::uint64_t total_bases{0U};
    std::uint64_t minimum_length{0U};
    std::uint64_t maximum_length{0U};
    std::uint64_t n50{0U};
    std::uint64_t canonical_bases{0U};
    std::uint64_t gc_bases{0U};
    std::uint64_t n_bases{0U};
    std::uint64_t ambiguous_bases{0U};

    [[nodiscard]] double average_length() const noexcept;
    [[nodiscard]] double gc_percent() const noexcept;
    [[nodiscard]] double n_percent() const noexcept;
    [[nodiscard]] double ambiguous_percent() const noexcept;
};

[[nodiscard]] FastaStatistics analyze_fasta(std::istream& input);
[[nodiscard]] std::string render_summary_json(const FastaStatistics& statistics);
[[nodiscard]] std::string render_summary_tsv(const FastaStatistics& statistics);

}  // namespace biocore::fasta_qc
