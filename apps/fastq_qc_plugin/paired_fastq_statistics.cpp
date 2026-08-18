#include "paired_fastq_statistics.hpp"

#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace biocore::fastq_qc {
namespace {

struct MateIdentity final {
    std::string_view core_id;
    std::optional<unsigned int> mate;
};

[[nodiscard]] std::string_view first_token(const std::string_view value) noexcept {
    const auto end = value.find_first_of(" \t");
    return value.substr(0U, end);
}

[[nodiscard]] MateIdentity parse_mate_identity(const std::string& header) {
    if (header.size() < 2U || header.front() != '@') {
        throw std::invalid_argument("Paired FASTQ header is invalid");
    }
    const std::string_view body{header.data() + 1U, header.size() - 1U};
    std::string_view primary = first_token(body);
    std::optional<unsigned int> mate;
    if (primary.size() > 2U && primary[primary.size() - 2U] == '/' &&
        (primary.back() == '1' || primary.back() == '2')) {
        mate = static_cast<unsigned int>(primary.back() - '0');
        primary.remove_suffix(2U);
    }

    const auto whitespace = body.find_first_of(" \t");
    if (whitespace != std::string_view::npos) {
        auto secondary = body.substr(whitespace + 1U);
        const auto first_non_space = secondary.find_first_not_of(" \t");
        if (first_non_space != std::string_view::npos) {
            secondary.remove_prefix(first_non_space);
            if (secondary.size() >= 2U && (secondary[0] == '1' || secondary[0] == '2') &&
                secondary[1] == ':') {
                const auto secondary_mate = static_cast<unsigned int>(secondary[0] - '0');
                if (mate.has_value() && *mate != secondary_mate) {
                    throw std::invalid_argument("FASTQ header contains conflicting mate indicators");
                }
                mate = secondary_mate;
            }
        }
    }
    if (primary.empty()) {
        throw std::invalid_argument("Paired FASTQ header identifier is empty");
    }
    return MateIdentity{primary, mate};
}

void validate_pair_impl(const FastqRecord& read1, const FastqRecord& read2) {
    const auto identity1 = parse_mate_identity(read1.header);
    const auto identity2 = parse_mate_identity(read2.header);
    if (identity1.core_id != identity2.core_id) {
        throw std::invalid_argument("Paired FASTQ mate identifiers do not match");
    }
    if (identity1.mate.has_value() && *identity1.mate != 1U) {
        throw std::invalid_argument("Paired FASTQ read1 record is marked as mate 2");
    }
    if (identity2.mate.has_value() && *identity2.mate != 2U) {
        throw std::invalid_argument("Paired FASTQ read2 record is marked as mate 1");
    }
}

[[nodiscard]] std::string format_number(const double value) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(6) << value;
    return output.str();
}

void append_json_metrics(
    std::ostringstream& output, const FastqStatistics& statistics, const std::string_view indent
) {
    output
        << indent << "\"readCount\": " << statistics.read_count << ",\n"
        << indent << "\"totalBases\": " << statistics.total_bases << ",\n"
        << indent << "\"minimumReadLength\": " << statistics.minimum_read_length << ",\n"
        << indent << "\"maximumReadLength\": " << statistics.maximum_read_length << ",\n"
        << indent << "\"averageReadLength\": " << format_number(statistics.average_read_length()) << ",\n"
        << indent << "\"canonicalBases\": " << statistics.canonical_bases << ",\n"
        << indent << "\"gcBases\": " << statistics.gc_bases << ",\n"
        << indent << "\"gcPercentCanonical\": " << format_number(statistics.gc_percent_canonical()) << ",\n"
        << indent << "\"nBases\": " << statistics.n_bases << ",\n"
        << indent << "\"nPercentAllBases\": " << format_number(statistics.n_percent_all_bases()) << ",\n"
        << indent << "\"ambiguousIupacBases\": " << statistics.ambiguous_iupac_bases << ",\n"
        << indent << "\"ambiguousIupacPercentAllBases\": " << format_number(statistics.ambiguous_percent_all_bases()) << ",\n"
        << indent << "\"averagePhred\": " << format_number(statistics.average_phred()) << ",\n"
        << indent << "\"minimumPhred\": " << statistics.minimum_phred << ",\n"
        << indent << "\"maximumPhred\": " << statistics.maximum_phred << ",\n"
        << indent << "\"q20Bases\": " << statistics.q20_bases << ",\n"
        << indent << "\"q20Percent\": " << format_number(statistics.q20_percent()) << ",\n"
        << indent << "\"q30Bases\": " << statistics.q30_bases << ",\n"
        << indent << "\"q30Percent\": " << format_number(statistics.q30_percent()) << '\n';
}

void append_tsv_metrics(
    std::ostringstream& output, const std::string_view prefix, const FastqStatistics& statistics
) {
    output
        << prefix << "read_count\t" << statistics.read_count << '\n'
        << prefix << "total_bases\t" << statistics.total_bases << '\n'
        << prefix << "minimum_read_length\t" << statistics.minimum_read_length << '\n'
        << prefix << "maximum_read_length\t" << statistics.maximum_read_length << '\n'
        << prefix << "average_read_length\t" << format_number(statistics.average_read_length()) << '\n'
        << prefix << "canonical_bases\t" << statistics.canonical_bases << '\n'
        << prefix << "gc_bases\t" << statistics.gc_bases << '\n'
        << prefix << "gc_percent_canonical\t" << format_number(statistics.gc_percent_canonical()) << '\n'
        << prefix << "n_bases\t" << statistics.n_bases << '\n'
        << prefix << "n_percent_all_bases\t" << format_number(statistics.n_percent_all_bases()) << '\n'
        << prefix << "ambiguous_iupac_bases\t" << statistics.ambiguous_iupac_bases << '\n'
        << prefix << "ambiguous_iupac_percent_all_bases\t" << format_number(statistics.ambiguous_percent_all_bases()) << '\n'
        << prefix << "average_phred\t" << format_number(statistics.average_phred()) << '\n'
        << prefix << "minimum_phred\t" << statistics.minimum_phred << '\n'
        << prefix << "maximum_phred\t" << statistics.maximum_phred << '\n'
        << prefix << "q20_bases\t" << statistics.q20_bases << '\n'
        << prefix << "q20_percent\t" << format_number(statistics.q20_percent()) << '\n'
        << prefix << "q30_bases\t" << statistics.q30_bases << '\n'
        << prefix << "q30_percent\t" << format_number(statistics.q30_percent()) << '\n';
}

}  // namespace

void validate_paired_fastq_records(const FastqRecord& read1, const FastqRecord& read2) {
    validate_pair_impl(read1, read2);
}

PairedFastqStatistics analyze_paired_fastq(std::istream& read1, std::istream& read2) {
    PairedFastqStatistics statistics;
    FastqRecordReader reader1{read1};
    FastqRecordReader reader2{read2};
    FastqRecord record1;
    FastqRecord record2;

    for (;;) {
        const bool has1 = reader1.read(record1);
        const bool has2 = reader2.read(record2);
        if (has1 != has2) {
            throw std::invalid_argument("Paired FASTQ files contain different record counts");
        }
        if (!has1) break;
        validate_paired_fastq_records(record1, record2);
        accumulate_fastq_record(statistics.read1, record1);
        accumulate_fastq_record(statistics.read2, record2);
        if (statistics.pair_count == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("Paired FASTQ pair count overflow");
        }
        ++statistics.pair_count;
    }

    if (statistics.pair_count == 0U) {
        throw std::invalid_argument("Paired FASTQ contains no read pairs");
    }
    statistics.combined = combine_fastq_statistics(statistics.read1, statistics.read2);
    return statistics;
}

std::string render_paired_summary_json(const PairedFastqStatistics& statistics) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "{\n"
           << "  \"schemaVersion\": 1,\n"
           << "  \"format\": \"FASTQ-PAIRED-DNA-PHRED33\",\n"
           << "  \"pairCount\": " << statistics.pair_count << ",\n"
           << "  \"read1\": {\n";
    append_json_metrics(output, statistics.read1, "    ");
    output << "  },\n  \"read2\": {\n";
    append_json_metrics(output, statistics.read2, "    ");
    output << "  },\n  \"combined\": {\n";
    append_json_metrics(output, statistics.combined, "    ");
    output << "  }\n}\n";
    return output.str();
}

std::string render_paired_summary_tsv(const PairedFastqStatistics& statistics) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << "metric\tvalue\n"
           << "pair_count\t" << statistics.pair_count << '\n';
    append_tsv_metrics(output, "read1_", statistics.read1);
    append_tsv_metrics(output, "read2_", statistics.read2);
    append_tsv_metrics(output, "combined_", statistics.combined);
    return output.str();
}

}  // namespace biocore::fastq_qc
