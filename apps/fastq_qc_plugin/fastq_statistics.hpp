#pragma once

#include <cstdint>
#include <istream>
#include <string>

namespace biocore::fastq_qc {

struct FastqStatistics final {
    std::uint64_t read_count{0U};
    std::uint64_t total_bases{0U};
    std::uint64_t minimum_read_length{0U};
    std::uint64_t maximum_read_length{0U};
    std::uint64_t canonical_bases{0U};
    std::uint64_t gc_bases{0U};
    std::uint64_t n_bases{0U};
    std::uint64_t ambiguous_iupac_bases{0U};
    std::uint64_t quality_sum{0U};
    std::uint64_t q20_bases{0U};
    std::uint64_t q30_bases{0U};
    std::uint32_t minimum_phred{0U};
    std::uint32_t maximum_phred{0U};

    [[nodiscard]] double average_read_length() const noexcept;
    [[nodiscard]] double gc_percent_canonical() const noexcept;
    [[nodiscard]] double n_percent_all_bases() const noexcept;
    [[nodiscard]] double ambiguous_percent_all_bases() const noexcept;
    [[nodiscard]] double average_phred() const noexcept;
    [[nodiscard]] double q20_percent() const noexcept;
    [[nodiscard]] double q30_percent() const noexcept;
};

struct FastqRecord final {
    std::string header;
    std::string sequence;
    std::string quality;
};

class FastqRecordReader final {
public:
    explicit FastqRecordReader(std::istream& input) noexcept;
    [[nodiscard]] bool read(FastqRecord& record);

private:
    std::istream& input_;
    bool first_record_{true};
};

void accumulate_fastq_record(FastqStatistics& statistics, const FastqRecord& record);
[[nodiscard]] FastqStatistics combine_fastq_statistics(
    const FastqStatistics& left, const FastqStatistics& right
);
[[nodiscard]] FastqStatistics analyze_fastq(std::istream& input);
[[nodiscard]] std::string render_summary_json(const FastqStatistics& statistics);
[[nodiscard]] std::string render_summary_tsv(const FastqStatistics& statistics);

}  // namespace biocore::fastq_qc
