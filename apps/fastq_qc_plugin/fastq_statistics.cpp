#include "fastq_statistics.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace biocore::fastq_qc {
namespace {

constexpr std::size_t maximum_line_bytes = 64U * 1024U * 1024U;

void checked_increment(std::uint64_t& value, const char* label) {
    if (value == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error(std::string{label} + " overflow");
    }
    ++value;
}

void checked_add(std::uint64_t& value, const std::uint64_t amount, const char* label) {
    if (amount > std::numeric_limits<std::uint64_t>::max() - value) {
        throw std::overflow_error(std::string{label} + " overflow");
    }
    value += amount;
}

[[nodiscard]] bool read_bounded_line(std::istream& input, std::string& line) {
    line.clear();
    bool saw_any = false;
    for (;;) {
        const int next = input.get();
        if (next == std::char_traits<char>::eof()) {
            if (input.bad()) {
                throw std::runtime_error("Unable to read FASTQ input");
            }
            return saw_any;
        }
        saw_any = true;
        const char value = static_cast<char>(next);
        if (value == '\n') {
            break;
        }
        if (line.size() >= maximum_line_bytes) {
            throw std::invalid_argument("FASTQ line exceeds the 64 MiB safety limit");
        }
        line.push_back(value);
    }
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return true;
}

void strip_initial_utf8_bom(std::string& line) {
    if (line.size() >= 3U &&
        static_cast<unsigned char>(line[0]) == 0xEFU &&
        static_cast<unsigned char>(line[1]) == 0xBBU &&
        static_cast<unsigned char>(line[2]) == 0xBFU) {
        line.erase(0U, 3U);
    }
}

[[nodiscard]] bool has_non_whitespace(std::string_view value) noexcept {
    for (const char c : value) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            return true;
        }
    }
    return false;
}

[[nodiscard]] char uppercase_ascii(const char value) noexcept {
    if (value >= 'a' && value <= 'z') {
        return static_cast<char>(value - ('a' - 'A'));
    }
    return value;
}

void record_base(FastqStatistics& statistics, const char raw) {
    const char base = uppercase_ascii(raw);
    switch (base) {
        case 'A':
        case 'T':
            checked_increment(statistics.canonical_bases, "Canonical base count");
            break;
        case 'C':
        case 'G':
            checked_increment(statistics.canonical_bases, "Canonical base count");
            checked_increment(statistics.gc_bases, "GC base count");
            break;
        case 'N':
            checked_increment(statistics.n_bases, "N base count");
            break;
        case 'R':
        case 'Y':
        case 'S':
        case 'W':
        case 'K':
        case 'M':
        case 'B':
        case 'D':
        case 'H':
        case 'V':
            checked_increment(
                statistics.ambiguous_iupac_bases, "Ambiguous IUPAC base count"
            );
            break;
        default:
            throw std::invalid_argument(
                std::string{"FASTQ contains a non-IUPAC DNA symbol: "} + raw
            );
    }
    checked_increment(statistics.total_bases, "Total base count");
}

[[nodiscard]] double percentage(
    const std::uint64_t numerator, const std::uint64_t denominator
) noexcept {
    if (denominator == 0U) {
        return 0.0;
    }
    return 100.0 * static_cast<double>(numerator) /
           static_cast<double>(denominator);
}

[[nodiscard]] std::string format_number(const double value) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(6) << value;
    return output.str();
}

}  // namespace

double FastqStatistics::average_read_length() const noexcept {
    if (read_count == 0U) {
        return 0.0;
    }
    return static_cast<double>(total_bases) / static_cast<double>(read_count);
}

double FastqStatistics::gc_percent_canonical() const noexcept {
    return percentage(gc_bases, canonical_bases);
}

double FastqStatistics::n_percent_all_bases() const noexcept {
    return percentage(n_bases, total_bases);
}

double FastqStatistics::ambiguous_percent_all_bases() const noexcept {
    return percentage(ambiguous_iupac_bases, total_bases);
}

double FastqStatistics::average_phred() const noexcept {
    if (total_bases == 0U) {
        return 0.0;
    }
    return static_cast<double>(quality_sum) / static_cast<double>(total_bases);
}

double FastqStatistics::q20_percent() const noexcept {
    return percentage(q20_bases, total_bases);
}

double FastqStatistics::q30_percent() const noexcept {
    return percentage(q30_bases, total_bases);
}

FastqRecordReader::FastqRecordReader(std::istream& input) noexcept : input_{input} {}

bool FastqRecordReader::read(FastqRecord& record) {
    std::string separator;
    if (!read_bounded_line(input_, record.header)) {
        return false;
    }
    if (first_record_) {
        strip_initial_utf8_bom(record.header);
        first_record_ = false;
    }
    if (record.header.empty() || record.header.front() != '@' ||
        !has_non_whitespace(std::string_view{record.header}.substr(1U))) {
        throw std::invalid_argument(
            "FASTQ record header must begin with '@' and contain an identifier"
        );
    }
    if (!read_bounded_line(input_, record.sequence)) {
        throw std::invalid_argument("FASTQ ended before the sequence line");
    }
    if (!read_bounded_line(input_, separator)) {
        throw std::invalid_argument("FASTQ ended before the '+' separator line");
    }
    if (!read_bounded_line(input_, record.quality)) {
        throw std::invalid_argument("FASTQ ended before the quality line");
    }
    if (record.sequence.empty()) {
        throw std::invalid_argument("FASTQ contains an empty sequence");
    }
    if (separator.empty() || separator.front() != '+') {
        throw std::invalid_argument("FASTQ separator line must begin with '+'");
    }
    if (record.quality.size() != record.sequence.size()) {
        throw std::invalid_argument("FASTQ sequence and quality lengths do not match");
    }
    return true;
}

void accumulate_fastq_record(FastqStatistics& statistics, const FastqRecord& record) {
    for (const char base : record.sequence) {
        record_base(statistics, base);
    }

    bool initialize_quality_range = statistics.read_count == 0U;
    for (const char raw : record.quality) {
        const auto encoded = static_cast<unsigned char>(raw);
        if (encoded < 33U || encoded > 126U) {
            throw std::invalid_argument(
                "FASTQ quality contains a character outside Phred+33 printable range"
            );
        }
        const std::uint32_t phred = static_cast<std::uint32_t>(encoded - 33U);
        checked_add(statistics.quality_sum, phred, "FASTQ quality sum");
        if (phred >= 20U) checked_increment(statistics.q20_bases, "Q20 base count");
        if (phred >= 30U) checked_increment(statistics.q30_bases, "Q30 base count");
        if (initialize_quality_range) {
            statistics.minimum_phred = phred;
            statistics.maximum_phred = phred;
            initialize_quality_range = false;
        } else {
            statistics.minimum_phred = std::min(statistics.minimum_phred, phred);
            statistics.maximum_phred = std::max(statistics.maximum_phred, phred);
        }
    }

    const auto length = static_cast<std::uint64_t>(record.sequence.size());
    if (statistics.read_count == 0U) {
        statistics.minimum_read_length = length;
        statistics.maximum_read_length = length;
    } else {
        statistics.minimum_read_length = std::min(statistics.minimum_read_length, length);
        statistics.maximum_read_length = std::max(statistics.maximum_read_length, length);
    }
    checked_increment(statistics.read_count, "FASTQ read count");
}

FastqStatistics combine_fastq_statistics(
    const FastqStatistics& left, const FastqStatistics& right
) {
    if (left.read_count == 0U || right.read_count == 0U) {
        throw std::invalid_argument("FASTQ statistics cannot combine an empty mate");
    }
    FastqStatistics combined;
    combined.read_count = left.read_count;
    checked_add(combined.read_count, right.read_count, "Combined FASTQ read count");
    combined.total_bases = left.total_bases;
    checked_add(combined.total_bases, right.total_bases, "Combined FASTQ total bases");
    combined.canonical_bases = left.canonical_bases;
    checked_add(combined.canonical_bases, right.canonical_bases, "Combined canonical bases");
    combined.gc_bases = left.gc_bases;
    checked_add(combined.gc_bases, right.gc_bases, "Combined GC bases");
    combined.n_bases = left.n_bases;
    checked_add(combined.n_bases, right.n_bases, "Combined N bases");
    combined.ambiguous_iupac_bases = left.ambiguous_iupac_bases;
    checked_add(
        combined.ambiguous_iupac_bases, right.ambiguous_iupac_bases,
        "Combined ambiguous IUPAC bases"
    );
    combined.quality_sum = left.quality_sum;
    checked_add(combined.quality_sum, right.quality_sum, "Combined FASTQ quality sum");
    combined.q20_bases = left.q20_bases;
    checked_add(combined.q20_bases, right.q20_bases, "Combined Q20 bases");
    combined.q30_bases = left.q30_bases;
    checked_add(combined.q30_bases, right.q30_bases, "Combined Q30 bases");
    combined.minimum_read_length = std::min(left.minimum_read_length, right.minimum_read_length);
    combined.maximum_read_length = std::max(left.maximum_read_length, right.maximum_read_length);
    combined.minimum_phred = std::min(left.minimum_phred, right.minimum_phred);
    combined.maximum_phred = std::max(left.maximum_phred, right.maximum_phred);
    return combined;
}

FastqStatistics analyze_fastq(std::istream& input) {
    FastqStatistics statistics;
    FastqRecordReader reader{input};
    FastqRecord record;
    while (reader.read(record)) {
        accumulate_fastq_record(statistics, record);
    }
    if (statistics.read_count == 0U) {
        throw std::invalid_argument("FASTQ contains no records");
    }
    return statistics;
}

std::string render_summary_json(const FastqStatistics& statistics) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output
        << "{\n"
        << "  \"schemaVersion\": 1,\n"
        << "  \"format\": \"FASTQ-DNA-PHRED33\",\n"
        << "  \"readCount\": " << statistics.read_count << ",\n"
        << "  \"totalBases\": " << statistics.total_bases << ",\n"
        << "  \"minimumReadLength\": " << statistics.minimum_read_length << ",\n"
        << "  \"maximumReadLength\": " << statistics.maximum_read_length << ",\n"
        << "  \"averageReadLength\": " << format_number(statistics.average_read_length()) << ",\n"
        << "  \"canonicalBases\": " << statistics.canonical_bases << ",\n"
        << "  \"gcBases\": " << statistics.gc_bases << ",\n"
        << "  \"gcPercentCanonical\": " << format_number(statistics.gc_percent_canonical()) << ",\n"
        << "  \"nBases\": " << statistics.n_bases << ",\n"
        << "  \"nPercentAllBases\": " << format_number(statistics.n_percent_all_bases()) << ",\n"
        << "  \"ambiguousIupacBases\": " << statistics.ambiguous_iupac_bases << ",\n"
        << "  \"ambiguousIupacPercentAllBases\": "
        << format_number(statistics.ambiguous_percent_all_bases()) << ",\n"
        << "  \"averagePhred\": " << format_number(statistics.average_phred()) << ",\n"
        << "  \"minimumPhred\": " << statistics.minimum_phred << ",\n"
        << "  \"maximumPhred\": " << statistics.maximum_phred << ",\n"
        << "  \"q20Bases\": " << statistics.q20_bases << ",\n"
        << "  \"q20Percent\": " << format_number(statistics.q20_percent()) << ",\n"
        << "  \"q30Bases\": " << statistics.q30_bases << ",\n"
        << "  \"q30Percent\": " << format_number(statistics.q30_percent()) << "\n"
        << "}\n";
    return output.str();
}

std::string render_summary_tsv(const FastqStatistics& statistics) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output
        << "metric\tvalue\n"
        << "read_count\t" << statistics.read_count << '\n'
        << "total_bases\t" << statistics.total_bases << '\n'
        << "minimum_read_length\t" << statistics.minimum_read_length << '\n'
        << "maximum_read_length\t" << statistics.maximum_read_length << '\n'
        << "average_read_length\t" << format_number(statistics.average_read_length()) << '\n'
        << "canonical_bases\t" << statistics.canonical_bases << '\n'
        << "gc_bases\t" << statistics.gc_bases << '\n'
        << "gc_percent_canonical\t" << format_number(statistics.gc_percent_canonical()) << '\n'
        << "n_bases\t" << statistics.n_bases << '\n'
        << "n_percent_all_bases\t" << format_number(statistics.n_percent_all_bases()) << '\n'
        << "ambiguous_iupac_bases\t" << statistics.ambiguous_iupac_bases << '\n'
        << "ambiguous_iupac_percent_all_bases\t"
        << format_number(statistics.ambiguous_percent_all_bases()) << '\n'
        << "average_phred\t" << format_number(statistics.average_phred()) << '\n'
        << "minimum_phred\t" << statistics.minimum_phred << '\n'
        << "maximum_phred\t" << statistics.maximum_phred << '\n'
        << "q20_bases\t" << statistics.q20_bases << '\n'
        << "q20_percent\t" << format_number(statistics.q20_percent()) << '\n'
        << "q30_bases\t" << statistics.q30_bases << '\n'
        << "q30_percent\t" << format_number(statistics.q30_percent()) << '\n';
    return output.str();
}

}  // namespace biocore::fastq_qc
