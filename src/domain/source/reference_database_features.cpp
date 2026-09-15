#include "biocore/domain/reference_database.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace biocore::domain {
namespace {

constexpr std::size_t kMaximumLineBytes = 16U * 1024U * 1024U;
constexpr std::size_t kMaximumTextBytes = 4096U;
constexpr std::size_t kMaximumFeatureRecords = 50'000'000U;
constexpr std::size_t kMaximumAttributes = 4096U;

enum class FeatureFormat : std::uint8_t { gff3, gtf };

[[nodiscard]] bool read_bounded_line(std::istream& input, std::string& line) {
    line.clear();
    char character = '\0';
    while (input.get(character)) {
        if (character == '\n') {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return true;
        }
        if (line.size() >= kMaximumLineBytes) {
            throw std::invalid_argument("reference feature line exceeds safety bound");
        }
        line.push_back(character);
    }
    if (!line.empty()) {
        if (line.back() == '\r') {
            line.pop_back();
        }
        return true;
    }
    return false;
}

[[nodiscard]] std::vector<std::string_view> split_nine_columns(const std::string_view line) {
    std::vector<std::string_view> columns;
    columns.reserve(9U);
    std::size_t begin = 0U;
    while (true) {
        const std::size_t tab = line.find('\t', begin);
        columns.push_back(line.substr(begin, tab == std::string_view::npos ? line.size() - begin : tab - begin));
        if (tab == std::string_view::npos) {
            break;
        }
        begin = tab + 1U;
        if (columns.size() > 9U) {
            break;
        }
    }
    if (columns.size() != 9U) {
        throw std::invalid_argument("GFF/GTF record must contain exactly nine columns");
    }
    return columns;
}

[[nodiscard]] std::uint64_t parse_coordinate(const std::string_view value, const char* const label) {
    std::uint64_t parsed = 0U;
    const char* const begin = value.data();
    const char* const end = begin + value.size();
    const auto [pointer, error] = std::from_chars(begin, end, parsed);
    if (value.empty() || error != std::errc{} || pointer != end || parsed == 0U) {
        throw std::invalid_argument(std::string{"invalid 1-based GFF/GTF "} + label);
    }
    return parsed;
}

[[nodiscard]] std::optional<double> parse_score(const std::string_view value) {
    if (value == ".") {
        return std::nullopt;
    }
    double parsed = 0.0;
    const char* const begin = value.data();
    const char* const end = begin + value.size();
    const auto [pointer, error] = std::from_chars(begin, end, parsed);
    if (value.empty() || error != std::errc{} || pointer != end || !std::isfinite(parsed)) {
        throw std::invalid_argument("invalid GFF/GTF score");
    }
    return parsed;
}

[[nodiscard]] std::optional<std::uint8_t> parse_phase(const std::string_view value) {
    if (value == ".") {
        return std::nullopt;
    }
    if (value.size() != 1U || value[0] < '0' || value[0] > '2') {
        throw std::invalid_argument("invalid GFF/GTF phase");
    }
    return static_cast<std::uint8_t>(value[0] - '0');
}

[[nodiscard]] std::string_view trim_spaces(std::string_view value) noexcept {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1U);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1U);
    }
    return value;
}

[[nodiscard]] bool valid_key(const std::string_view key) noexcept {
    if (key.empty() || key.size() > kMaximumTextBytes) {
        return false;
    }
    for (const char character : key) {
        const bool alpha = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
        const bool digit = character >= '0' && character <= '9';
        if (!alpha && !digit && character != '.' && character != '_' && character != '-' && character != ':') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::vector<ReferenceDatabaseAttribute> parse_gff3_attributes(const std::string_view field) {
    std::vector<ReferenceDatabaseAttribute> attributes;
    if (field == "." || field.empty()) {
        return attributes;
    }
    std::size_t begin = 0U;
    while (begin <= field.size()) {
        const std::size_t separator = field.find(';', begin);
        std::string_view token = trim_spaces(field.substr(
            begin,
            separator == std::string_view::npos ? field.size() - begin : separator - begin
        ));
        if (!token.empty()) {
            const std::size_t equals = token.find('=');
            if (equals == std::string_view::npos) {
                throw std::invalid_argument("GFF3 attribute is missing '='");
            }
            const std::string_view key = trim_spaces(token.substr(0U, equals));
            const std::string_view value = token.substr(equals + 1U);
            if (!valid_key(key) || value.size() > kMaximumTextBytes) {
                throw std::invalid_argument("GFF3 attribute exceeds supported bounds");
            }
            attributes.push_back(ReferenceDatabaseAttribute{std::string{key}, std::string{value}});
            if (attributes.size() > kMaximumAttributes) {
                throw std::invalid_argument("GFF3 record has too many attributes");
            }
        }
        if (separator == std::string_view::npos) {
            break;
        }
        begin = separator + 1U;
    }
    return attributes;
}

[[nodiscard]] std::vector<ReferenceDatabaseAttribute> parse_gtf_attributes(const std::string_view field) {
    std::vector<ReferenceDatabaseAttribute> attributes;
    if (field == "." || field.empty()) {
        return attributes;
    }
    std::size_t position = 0U;
    while (position < field.size()) {
        while (position < field.size() && (field[position] == ' ' || field[position] == '\t' || field[position] == ';')) {
            ++position;
        }
        if (position == field.size()) {
            break;
        }
        const std::size_t key_begin = position;
        while (position < field.size() && field[position] != ' ' && field[position] != '\t') {
            ++position;
        }
        const std::string_view key = field.substr(key_begin, position - key_begin);
        if (!valid_key(key)) {
            throw std::invalid_argument("GTF attribute key is invalid");
        }
        while (position < field.size() && (field[position] == ' ' || field[position] == '\t')) {
            ++position;
        }
        if (position >= field.size() || field[position] != '"') {
            throw std::invalid_argument("GTF attribute value must be quoted");
        }
        ++position;
        std::string value;
        while (position < field.size()) {
            const char character = field[position++];
            if (character == '"') {
                break;
            }
            if (character == '\\' && position < field.size()) {
                value.push_back(field[position++]);
            } else {
                value.push_back(character);
            }
            if (value.size() > kMaximumTextBytes) {
                throw std::invalid_argument("GTF attribute value exceeds safety bound");
            }
        }
        if (position == 0U || field[position - 1U] != '"') {
            throw std::invalid_argument("unterminated GTF attribute value");
        }
        while (position < field.size() && (field[position] == ' ' || field[position] == '\t')) {
            ++position;
        }
        if (position < field.size() && field[position] == ';') {
            ++position;
        }
        attributes.push_back(ReferenceDatabaseAttribute{std::string{key}, std::move(value)});
        if (attributes.size() > kMaximumAttributes) {
            throw std::invalid_argument("GTF record has too many attributes");
        }
    }
    return attributes;
}

[[nodiscard]] ReferenceDatabase parse_feature_database(
    std::istream& input,
    ReferenceDatabaseMetadata metadata,
    ContigTable contigs,
    const FeatureFormat format
) {
    std::vector<ReferenceFeatureRecord> features;
    std::string line;
    std::size_t source_ordinal = 0U;
    while (read_bounded_line(input, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (features.size() >= kMaximumFeatureRecords) {
            throw std::invalid_argument("reference feature count exceeds safety bound");
        }
        const auto columns = split_nine_columns(line);
        const auto contig_id = contigs.resolve(columns[0]);
        if (!contig_id.has_value()) {
            throw std::invalid_argument("GFF/GTF record references unknown contig");
        }
        const std::uint64_t start_one_based = parse_coordinate(columns[3], "start");
        const std::uint64_t end_one_based = parse_coordinate(columns[4], "end");
        if (end_one_based < start_one_based) {
            throw std::invalid_argument("GFF/GTF end precedes start");
        }
        if (columns[1].size() > kMaximumTextBytes || columns[2].empty() || columns[2].size() > kMaximumTextBytes) {
            throw std::invalid_argument("GFF/GTF source or feature type exceeds safety bound");
        }
        if (columns[6].size() != 1U || (columns[6][0] != '+' && columns[6][0] != '-'
            && columns[6][0] != '.' && columns[6][0] != '?')) {
            throw std::invalid_argument("invalid GFF/GTF strand");
        }
        ReferenceFeatureRecord feature;
        feature.locus = GenomicInterval{*contig_id, start_one_based - 1U, end_one_based};
        feature.source = std::string{columns[1]};
        feature.feature_type = std::string{columns[2]};
        feature.score = parse_score(columns[5]);
        feature.strand = columns[6][0];
        feature.phase = parse_phase(columns[7]);
        feature.source_ordinal = source_ordinal++;
        feature.attributes = format == FeatureFormat::gff3
            ? parse_gff3_attributes(columns[8])
            : parse_gtf_attributes(columns[8]);
        features.push_back(std::move(feature));
    }
    if (!input.eof() && input.fail()) {
        throw std::runtime_error("GFF/GTF read failure");
    }
    return ReferenceDatabase::build(std::move(metadata), std::move(contigs), {}, std::move(features));
}

}  // namespace

ReferenceDatabase ReferenceDatabase::from_gff3(
    std::istream& input,
    ReferenceDatabaseMetadata metadata,
    ContigTable contigs
) {
    return parse_feature_database(input, std::move(metadata), std::move(contigs), FeatureFormat::gff3);
}

ReferenceDatabase ReferenceDatabase::from_gtf(
    std::istream& input,
    ReferenceDatabaseMetadata metadata,
    ContigTable contigs
) {
    return parse_feature_database(input, std::move(metadata), std::move(contigs), FeatureFormat::gtf);
}

}  // namespace biocore::domain
