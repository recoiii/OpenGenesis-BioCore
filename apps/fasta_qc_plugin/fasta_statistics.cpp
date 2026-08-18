#include "fasta_statistics.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace biocore::fasta_qc {
namespace {

constexpr std::size_t maximum_header_bytes = 4096U;
constexpr std::size_t maximum_sequence_count = 10'000'000U;

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

[[nodiscard]] char uppercase_ascii(const char value) noexcept {
    if (value >= 'a' && value <= 'z') {
        return static_cast<char>(value - ('a' - 'A'));
    }
    return value;
}

void record_base(FastaStatistics& statistics, const char raw) {
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
            checked_increment(statistics.ambiguous_bases, "Ambiguous base count");
            break;
        default:
            throw std::invalid_argument(
                std::string{"FASTA contains a non-IUPAC DNA symbol: "} + raw
            );
    }
    checked_increment(statistics.total_bases, "Total base count");
}

void finalize_sequence(
    FastaStatistics& statistics,
    std::vector<std::uint64_t>& lengths,
    const std::uint64_t current_length
) {
    if (current_length == 0U) {
        throw std::invalid_argument("FASTA contains an empty sequence");
    }
    if (lengths.size() >= maximum_sequence_count) {
        throw std::invalid_argument("FASTA contains too many sequence records");
    }

    checked_increment(statistics.sequence_count, "Sequence count");
    lengths.push_back(current_length);
    if (statistics.sequence_count == 1U) {
        statistics.minimum_length = current_length;
        statistics.maximum_length = current_length;
    } else {
        statistics.minimum_length = std::min(statistics.minimum_length, current_length);
        statistics.maximum_length = std::max(statistics.maximum_length, current_length);
    }
}

[[nodiscard]] double percentage(
    const std::uint64_t numerator,
    const std::uint64_t denominator
) noexcept {
    if (denominator == 0U) return 0.0;
    return 100.0 * static_cast<double>(numerator) / static_cast<double>(denominator);
}

[[nodiscard]] std::string format_number(const double value) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output << std::fixed << std::setprecision(6) << value;
    return output.str();
}

}  // namespace

double FastaStatistics::average_length() const noexcept {
    if (sequence_count == 0U) return 0.0;
    return static_cast<double>(total_bases) / static_cast<double>(sequence_count);
}

double FastaStatistics::gc_percent() const noexcept {
    return percentage(gc_bases, canonical_bases);
}

double FastaStatistics::n_percent() const noexcept {
    return percentage(n_bases, total_bases);
}

double FastaStatistics::ambiguous_percent() const noexcept {
    return percentage(ambiguous_bases, total_bases);
}

FastaStatistics analyze_fasta(std::istream& input) {
    FastaStatistics statistics;
    std::vector<std::uint64_t> lengths;

    bool saw_header = false;
    bool in_header = false;
    bool at_line_start = true;
    bool header_has_non_space = false;
    std::size_t header_bytes = 0U;
    std::uint64_t current_length = 0U;

    const auto finish_header = [&]() {
        if (header_bytes == 0U || !header_has_non_space) {
            throw std::invalid_argument("FASTA header must contain non-whitespace text");
        }
        in_header = false;
        at_line_start = true;
    };

    const auto process_character = [&](const char character) {
        const bool newline = character == '\n' || character == '\r';

        if (in_header) {
            if (newline) {
                finish_header();
                return;
            }
            if (header_bytes >= maximum_header_bytes) {
                throw std::invalid_argument("FASTA header exceeds the supported size limit");
            }
            ++header_bytes;
            if (std::isspace(static_cast<unsigned char>(character)) == 0) {
                header_has_non_space = true;
            }
            at_line_start = false;
            return;
        }

        if (newline) {
            at_line_start = true;
            return;
        }

        if (at_line_start && character == '>') {
            if (saw_header) {
                finalize_sequence(statistics, lengths, current_length);
            }
            saw_header = true;
            in_header = true;
            at_line_start = false;
            header_bytes = 0U;
            header_has_non_space = false;
            current_length = 0U;
            return;
        }

        if (!saw_header) {
            throw std::invalid_argument("FASTA sequence data appears before the first header");
        }
        if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            throw std::invalid_argument("FASTA sequence lines must not contain whitespace");
        }

        record_base(statistics, character);
        checked_increment(current_length, "Sequence length");
        at_line_start = false;
    };

    std::array<char, 3U> prefix{};
    std::size_t prefix_size = 0U;
    for (; prefix_size < prefix.size(); ++prefix_size) {
        if (!input.get(prefix[prefix_size])) break;
    }

    std::size_t prefix_start = 0U;
    if (prefix_size == 3U &&
        static_cast<unsigned char>(prefix[0]) == 0xEFU &&
        static_cast<unsigned char>(prefix[1]) == 0xBBU &&
        static_cast<unsigned char>(prefix[2]) == 0xBFU) {
        prefix_start = 3U;
    }
    for (std::size_t index = prefix_start; index < prefix_size; ++index) {
        process_character(prefix[index]);
    }

    char character = '\0';
    while (input.get(character)) {
        process_character(character);
    }
    if (input.bad()) {
        throw std::runtime_error("Unable to read FASTA input");
    }

    if (in_header) {
        finish_header();
    }
    if (!saw_header) {
        throw std::invalid_argument("FASTA contains no sequence records");
    }

    finalize_sequence(statistics, lengths, current_length);

    const std::uint64_t expected_total =
        statistics.canonical_bases + statistics.n_bases + statistics.ambiguous_bases;
    if (expected_total != statistics.total_bases) {
        throw std::logic_error("FASTA base accounting invariant failed");
    }

    std::sort(lengths.begin(), lengths.end(), std::greater<>{});
    const std::uint64_t threshold =
        (statistics.total_bases / 2U) + (statistics.total_bases % 2U);

    std::uint64_t cumulative = 0U;
    for (const std::uint64_t length : lengths) {
        checked_add(cumulative, length, "N50 cumulative length");
        if (cumulative >= threshold) {
            statistics.n50 = length;
            break;
        }
    }
    if (statistics.n50 == 0U) {
        throw std::logic_error("Unable to derive FASTA N50");
    }

    return statistics;
}

std::string render_summary_json(const FastaStatistics& statistics) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output
        << "{\n"
        << "  \"schemaVersion\": 1,\n"
        << "  \"format\": \"FASTA-DNA\",\n"
        << "  \"sequenceCount\": " << statistics.sequence_count << ",\n"
        << "  \"totalBases\": " << statistics.total_bases << ",\n"
        << "  \"minimumLength\": " << statistics.minimum_length << ",\n"
        << "  \"maximumLength\": " << statistics.maximum_length << ",\n"
        << "  \"averageLength\": " << format_number(statistics.average_length()) << ",\n"
        << "  \"n50\": " << statistics.n50 << ",\n"
        << "  \"canonicalBases\": " << statistics.canonical_bases << ",\n"
        << "  \"gcBases\": " << statistics.gc_bases << ",\n"
        << "  \"gcPercentCanonical\": " << format_number(statistics.gc_percent()) << ",\n"
        << "  \"nBases\": " << statistics.n_bases << ",\n"
        << "  \"nPercentAllBases\": " << format_number(statistics.n_percent()) << ",\n"
        << "  \"ambiguousIupacBases\": " << statistics.ambiguous_bases << ",\n"
        << "  \"ambiguousIupacPercentAllBases\": "
        << format_number(statistics.ambiguous_percent()) << "\n"
        << "}\n";
    return output.str();
}

std::string render_summary_tsv(const FastaStatistics& statistics) {
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output
        << "metric\tvalue\n"
        << "sequence_count\t" << statistics.sequence_count << '\n'
        << "total_bases\t" << statistics.total_bases << '\n'
        << "minimum_length\t" << statistics.minimum_length << '\n'
        << "maximum_length\t" << statistics.maximum_length << '\n'
        << "average_length\t" << format_number(statistics.average_length()) << '\n'
        << "n50\t" << statistics.n50 << '\n'
        << "canonical_bases\t" << statistics.canonical_bases << '\n'
        << "gc_bases\t" << statistics.gc_bases << '\n'
        << "gc_percent_canonical\t" << format_number(statistics.gc_percent()) << '\n'
        << "n_bases\t" << statistics.n_bases << '\n'
        << "n_percent_all_bases\t" << format_number(statistics.n_percent()) << '\n'
        << "ambiguous_iupac_bases\t" << statistics.ambiguous_bases << '\n'
        << "ambiguous_iupac_percent_all_bases\t"
        << format_number(statistics.ambiguous_percent()) << '\n';
    return output.str();
}

}  // namespace biocore::fasta_qc
