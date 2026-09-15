#include "biocore/domain/reference_database.hpp"

#include "biocore/domain/variant_types.hpp"

#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace biocore::domain {
namespace {

constexpr std::size_t kMaximumLineBytes = 16U * 1024U * 1024U;
constexpr std::size_t kMaximumColumns = 1030U;
constexpr std::size_t kMaximumReferenceRecords = 50'000'000U;
constexpr std::size_t kMaximumIdentifierBytes = 1024U;
constexpr std::size_t kMaximumAlleleBytes = 1024U * 1024U;
constexpr std::size_t kMaximumAttributeValueBytes = 4096U;

[[nodiscard]] bool valid_attribute_key(const std::string_view value) noexcept {
    if (value.empty() || value.size() > kMaximumIdentifierBytes) {
        return false;
    }
    for (const char character : value) {
        const bool alpha = (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z');
        const bool digit = character >= '0' && character <= '9';
        if (!alpha && !digit && character != '.' && character != '_' && character != '-') {
            return false;
        }
    }
    return true;
}

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
            throw std::invalid_argument("reference database TSV line exceeds safety bound");
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

[[nodiscard]] std::vector<std::string_view> split_tabs(const std::string_view line) {
    std::vector<std::string_view> columns;
    std::size_t begin = 0U;
    while (begin <= line.size()) {
        const std::size_t tab = line.find('\t', begin);
        columns.push_back(line.substr(begin, tab == std::string_view::npos ? line.size() - begin : tab - begin));
        if (columns.size() > kMaximumColumns) {
            throw std::invalid_argument("reference database TSV has too many columns");
        }
        if (tab == std::string_view::npos) {
            break;
        }
        begin = tab + 1U;
    }
    return columns;
}

[[nodiscard]] std::uint64_t parse_uint64(const std::string_view value, const char* const field) {
    if (value.empty()) {
        throw std::invalid_argument(std::string{"reference database TSV missing "} + field);
    }
    std::uint64_t parsed = 0U;
    const char* const begin = value.data();
    const char* const end = begin + value.size();
    const auto [pointer, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || pointer != end) {
        throw std::invalid_argument(std::string{"reference database TSV invalid "} + field);
    }
    return parsed;
}

[[nodiscard]] bool is_symbolic_alt(const std::string_view alternate) noexcept {
    return alternate == "*" || (alternate.size() >= 3U && alternate.front() == '<' && alternate.back() == '>');
}

}  // namespace

ReferenceDatabase ReferenceDatabase::from_tsv(
    std::istream& input,
    ReferenceDatabaseMetadata metadata,
    ContigTable contigs
) {
    std::string line;
    if (!read_bounded_line(input, line) || line != "#OpenGenesis-BioCore-ReferenceDB\t1") {
        throw std::invalid_argument("reference database TSV magic/version header is missing or unsupported");
    }
    if (!read_bounded_line(input, line)) {
        throw std::invalid_argument("reference database TSV column header is missing");
    }
    const auto header = split_tabs(line);
    if (header.size() < 6U || header[0] != "contig" || header[1] != "start" || header[2] != "end"
        || header[3] != "ref" || header[4] != "alt" || header[5] != "record_id") {
        throw std::invalid_argument("reference database TSV required columns are invalid");
    }

    std::vector<std::string> attribute_keys;
    attribute_keys.reserve(header.size() - 6U);
    std::unordered_set<std::string> unique_attribute_keys;
    unique_attribute_keys.reserve(header.size() - 6U);
    for (std::size_t index = 6U; index < header.size(); ++index) {
        if (!header[index].starts_with("attr:") || header[index].size() <= 5U) {
            throw std::invalid_argument("reference database TSV optional columns must use attr:<key>");
        }
        std::string key{header[index].substr(5U)};
        if (!valid_attribute_key(key)) {
            throw std::invalid_argument("reference database TSV attribute key is invalid");
        }
        if (!unique_attribute_keys.emplace(key).second) {
            throw std::invalid_argument("reference database TSV contains duplicate attribute columns");
        }
        attribute_keys.push_back(std::move(key));
    }

    std::vector<ReferenceDatabaseRecord> records;
    while (read_bounded_line(input, line)) {
        if (line.empty()) {
            continue;
        }
        const auto columns = split_tabs(line);
        if (columns.size() != header.size()) {
            throw std::invalid_argument("reference database TSV row cardinality does not match header");
        }
        const auto contig_id = contigs.resolve(columns[0]);
        if (!contig_id.has_value()) {
            throw std::invalid_argument("reference database TSV references an unknown contig");
        }
        const std::uint64_t start = parse_uint64(columns[1], "start");
        const std::uint64_t end = parse_uint64(columns[2], "end");
        if (columns[3].empty() || columns[4].empty() || columns[5].empty()) {
            throw std::invalid_argument("reference database TSV REF/ALT/record_id must not be empty");
        }
        if (columns[3].size() > kMaximumAlleleBytes || columns[4].size() > kMaximumAlleleBytes
            || columns[5].size() > kMaximumIdentifierBytes) {
            throw std::invalid_argument("reference database TSV allele or record ID exceeds safety bound");
        }
        if (records.size() >= kMaximumReferenceRecords) {
            throw std::invalid_argument("reference database TSV record count exceeds safety bound");
        }

        ReferenceDatabaseRecord record;
        record.locus = GenomicInterval{*contig_id, start, end};
        record.reference = Allele{std::string{columns[3]}, VariantType::unknown, false};
        const bool symbolic = is_symbolic_alt(columns[4]);
        record.alternate = Allele{
            std::string{columns[4]},
            classify_variant(columns[3], columns[4], symbolic),
            symbolic
        };
        record.record_id = std::string{columns[5]};
        record.attributes.reserve(attribute_keys.size());
        for (std::size_t attribute = 0U; attribute < attribute_keys.size(); ++attribute) {
            const std::string_view raw_value = columns[attribute + 6U];
            if (raw_value.size() > kMaximumAttributeValueBytes) {
                throw std::invalid_argument("reference database TSV attribute value exceeds safety bound");
            }
            record.attributes.push_back(ReferenceDatabaseAttribute{
                attribute_keys[attribute],
                raw_value == "." ? std::nullopt : std::optional<std::string>{std::string{raw_value}}
            });
        }
        records.push_back(std::move(record));
    }
    if (!input.eof() && input.fail()) {
        throw std::runtime_error("reference database TSV read failure");
    }
    return build(std::move(metadata), std::move(contigs), std::move(records));
}

}  // namespace biocore::domain
