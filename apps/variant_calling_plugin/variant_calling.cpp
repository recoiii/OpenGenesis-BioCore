#include "variant_calling.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <zlib.h>

namespace biocore::variant_calling {
namespace {

constexpr std::uint64_t maximum_reference_file_bytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t maximum_reference_bases = 500000000ULL;
constexpr std::size_t maximum_reference_line_bytes = 64U * 1024U * 1024U;
constexpr std::size_t maximum_contig_name_bytes = 1024U;
constexpr std::size_t maximum_sam_line_bytes = 64U * 1024U * 1024U;
constexpr std::uint32_t maximum_bam_header_text_bytes = 64U * 1024U * 1024U;
constexpr std::uint32_t maximum_bam_reference_count = 1000000U;
constexpr std::uint32_t maximum_bam_reference_name_bytes = 1024U * 1024U;
constexpr std::int32_t maximum_bam_record_bytes = 64 * 1024 * 1024;
constexpr std::uint16_t flag_unmapped = 0x4U;
constexpr std::uint16_t flag_secondary = 0x100U;
constexpr std::uint16_t flag_qc_fail = 0x200U;
constexpr std::uint16_t flag_duplicate = 0x400U;
constexpr std::uint16_t flag_supplementary = 0x800U;
constexpr std::size_t maximum_pileup_sites = 50000000U;

struct CigarOperation final {
    std::uint64_t length{0U};
    char operation{'M'};
};

struct SitePileup final {
    std::array<std::uint64_t, 4> counts{};
    std::array<std::uint64_t, 4> quality_sums{};
};

using ContigPileup = std::map<std::uint64_t, SitePileup>;
using Pileup = std::map<std::string, ContigPileup>;

[[nodiscard]] char uppercase_ascii(const char value) noexcept {
    return value >= 'a' && value <= 'z' ? static_cast<char>(value - 'a' + 'A') : value;
}

[[nodiscard]] bool valid_iupac(const char value) noexcept {
    switch (uppercase_ascii(value)) {
        case 'A': case 'C': case 'G': case 'T': case 'U': case 'R': case 'Y': case 'S':
        case 'W': case 'K': case 'M': case 'B': case 'D': case 'H': case 'V': case 'N': return true;
        default: return false;
    }
}

[[nodiscard]] std::optional<std::size_t> canonical_index(const char value) noexcept {
    switch (uppercase_ascii(value)) {
        case 'A': return 0U;
        case 'C': return 1U;
        case 'G': return 2U;
        case 'T': return 3U;
        default: return std::nullopt;
    }
}

[[nodiscard]] char canonical_base(const std::size_t index) {
    static constexpr std::array<char, 4> bases{'A', 'C', 'G', 'T'};
    if (index >= bases.size()) throw std::logic_error("Canonical base index is invalid");
    return bases[index];
}

[[nodiscard]] bool read_bounded_line(
    std::istream& input,
    std::string& line,
    const std::size_t maximum_bytes,
    const std::string_view label
) {
    line.clear();
    bool saw = false;
    for (;;) {
        const int next = input.get();
        if (next == std::char_traits<char>::eof()) {
            if (input.bad()) throw std::runtime_error("Unable to read " + std::string{label});
            return saw;
        }
        saw = true;
        const char value = static_cast<char>(next);
        if (value == '\n') break;
        if (line.size() >= maximum_bytes) {
            throw std::invalid_argument(std::string{label} + " line exceeds the safety limit");
        }
        line.push_back(value);
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
}

void strip_utf8_bom(std::string& line) {
    if (line.size() >= 3U && static_cast<unsigned char>(line[0]) == 0xEFU &&
        static_cast<unsigned char>(line[1]) == 0xBBU &&
        static_cast<unsigned char>(line[2]) == 0xBFU) {
        line.erase(0U, 3U);
    }
}

[[nodiscard]] std::string first_token(const std::string_view value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string_view::npos) return {};
    const auto end = value.find_first_of(" \t", first);
    return std::string{value.substr(first, end == std::string_view::npos ? value.size() - first : end - first)};
}

[[nodiscard]] std::vector<std::string_view> split_tabs(const std::string& line) {
    std::vector<std::string_view> fields;
    std::size_t begin = 0U;
    for (;;) {
        const auto end = line.find('\t', begin);
        fields.emplace_back(line.data() + begin, (end == std::string::npos ? line.size() : end) - begin);
        if (end == std::string::npos) break;
        begin = end + 1U;
    }
    return fields;
}

template <typename T>
[[nodiscard]] T parse_unsigned(const std::string_view value, const std::string_view label) {
    if (value.empty() || value.front() == '-') {
        throw std::invalid_argument(std::string{label} + " is invalid");
    }
    unsigned long long parsed = 0U;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed, 10);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
        parsed > std::numeric_limits<T>::max()) {
        throw std::invalid_argument(std::string{label} + " is invalid");
    }
    return static_cast<T>(parsed);
}

[[nodiscard]] std::vector<CigarOperation> parse_cigar(const std::string_view cigar) {
    if (cigar.empty() || cigar == "*") throw std::invalid_argument("Mapped alignment CIGAR is invalid");
    std::vector<CigarOperation> operations;
    std::uint64_t length = 0U;
    bool have_digits = false;
    for (const char value : cigar) {
        if (value >= '0' && value <= '9') {
            have_digits = true;
            const auto digit = static_cast<std::uint64_t>(value - '0');
            if (length > (std::numeric_limits<std::uint64_t>::max() - digit) / 10U) {
                throw std::invalid_argument("CIGAR length overflows");
            }
            length = length * 10U + digit;
            continue;
        }
        if (!have_digits || length == 0U || std::string_view{"MIDNSHP=X"}.find(value) == std::string_view::npos) {
            throw std::invalid_argument("CIGAR is malformed or unsupported");
        }
        operations.push_back({length, value});
        length = 0U;
        have_digits = false;
    }
    if (have_digits || operations.empty()) throw std::invalid_argument("CIGAR is malformed");
    return operations;
}

[[nodiscard]] std::uint64_t read_span(const std::vector<CigarOperation>& operations) {
    std::uint64_t result = 0U;
    for (const auto& op : operations) {
        if (op.operation == 'M' || op.operation == 'I' || op.operation == 'S' || op.operation == '=' || op.operation == 'X') {
            if (result > std::numeric_limits<std::uint64_t>::max() - op.length) throw std::invalid_argument("CIGAR read span overflows");
            result += op.length;
        }
    }
    return result;
}

[[nodiscard]] std::uint64_t reference_span(const std::vector<CigarOperation>& operations) {
    std::uint64_t result = 0U;
    for (const auto& op : operations) {
        if (op.operation == 'M' || op.operation == 'D' || op.operation == 'N' || op.operation == '=' || op.operation == 'X') {
            if (result > std::numeric_limits<std::uint64_t>::max() - op.length) throw std::invalid_argument("CIGAR reference span overflows");
            result += op.length;
        }
    }
    return result;
}

[[nodiscard]] std::unordered_map<std::string, std::size_t> reference_lookup(
    const std::vector<ReferenceContig>& reference
) {
    std::unordered_map<std::string, std::size_t> lookup;
    lookup.reserve(reference.size());
    for (std::size_t i = 0U; i < reference.size(); ++i) {
        lookup.emplace(reference[i].name, i);
    }
    return lookup;
}

void observe_alignment(
    VariantCallingStatistics& stats,
    Pileup& pileup,
    std::size_t& pileup_sites,
    const ReferenceContig& contig,
    const std::uint64_t zero_based_position,
    const std::uint16_t flag,
    const std::uint32_t mapq,
    const std::vector<CigarOperation>& operations,
    const std::string_view sequence,
    const std::vector<std::uint8_t>& qualities,
    const VariantCallingOptions& options
) {
    ++stats.total_records;
    if ((flag & flag_secondary) != 0U) {
        ++stats.secondary_records;
        return;
    }
    if ((flag & flag_supplementary) != 0U) {
        ++stats.supplementary_records;
        return;
    }
    if ((flag & flag_unmapped) != 0U) {
        ++stats.unmapped_records;
        return;
    }
    if ((flag & flag_duplicate) != 0U) {
        ++stats.duplicate_records;
        return;
    }
    if ((flag & flag_qc_fail) != 0U) {
        ++stats.qc_failed_records;
        return;
    }
    if (mapq == 255U) {
        ++stats.unavailable_mapq_records;
        return;
    }
    if (mapq < options.minimum_mapq) {
        ++stats.low_mapq_records;
        return;
    }
    if (sequence.empty() || qualities.size() != sequence.size()) {
        throw std::invalid_argument("Eligible mapped alignment requires sequence and base qualities");
    }
    if (read_span(operations) != sequence.size()) {
        throw std::invalid_argument("CIGAR read span does not match alignment sequence length");
    }
    const auto span = reference_span(operations);
    if (zero_based_position > contig.sequence.size() || span > contig.sequence.size() - zero_based_position) {
        throw std::invalid_argument("Mapped CIGAR span exceeds the reference contig");
    }

    ++stats.eligible_records;
    std::uint64_t ref_index = zero_based_position;
    std::size_t read_index = 0U;
    for (const auto& op : operations) {
        if (op.operation == 'M' || op.operation == '=' || op.operation == 'X') {
            for (std::uint64_t offset = 0U; offset < op.length; ++offset) {
                if (read_index >= sequence.size() || ref_index >= contig.sequence.size()) {
                    throw std::invalid_argument("CIGAR traversal exceeds sequence/reference bounds");
                }
                const auto ref_base_index = canonical_index(contig.sequence[static_cast<std::size_t>(ref_index)]);
                if (!ref_base_index.has_value()) {
                    ++stats.noncanonical_reference_observations;
                    ++read_index;
                    ++ref_index;
                    continue;
                }
                const auto read_base_index = sequence[read_index] == '='
                    ? ref_base_index
                    : canonical_index(sequence[read_index]);
                if (!read_base_index.has_value()) {
                    ++stats.ambiguous_read_base_observations;
                    ++read_index;
                    ++ref_index;
                    continue;
                }
                const auto quality = qualities[read_index];
                if (quality == 0xFFU || quality < options.minimum_base_quality) {
                    ++stats.low_quality_base_observations;
                    ++read_index;
                    ++ref_index;
                    continue;
                }
                auto& contig_sites = pileup[contig.name];
                auto found = contig_sites.find(ref_index);
                if (found == contig_sites.end()) {
                    if (pileup_sites >= maximum_pileup_sites) {
                        throw std::invalid_argument("Variant pileup exceeds the 50 million site safety bound");
                    }
                    found = contig_sites.emplace(ref_index, SitePileup{}).first;
                    ++pileup_sites;
                }
                ++found->second.counts[*read_base_index];
                found->second.quality_sums[*read_base_index] += quality;
                ++stats.callable_base_observations;
                ++read_index;
                ++ref_index;
            }
        } else if (op.operation == 'I' || op.operation == 'S') {
            if (op.length > sequence.size() - read_index) throw std::invalid_argument("CIGAR read operation exceeds sequence length");
            read_index += static_cast<std::size_t>(op.length);
        } else if (op.operation == 'D' || op.operation == 'N') {
            if (op.length > contig.sequence.size() - static_cast<std::size_t>(ref_index)) {
                throw std::invalid_argument("CIGAR reference operation exceeds contig length");
            }
            ref_index += op.length;
        } else if (op.operation == 'H' || op.operation == 'P') {
            continue;
        } else {
            throw std::invalid_argument("Unsupported CIGAR operation");
        }
    }
    if (read_index != sequence.size()) throw std::invalid_argument("CIGAR does not consume the alignment sequence");
}

void finalize_calls(
    VariantCallingStatistics& stats,
    const Pileup& pileup,
    const std::vector<ReferenceContig>& reference,
    const VariantCallingOptions& options
) {
    const auto lookup = reference_lookup(reference);
    for (const auto& [contig_name, sites] : pileup) {
        const auto ref_it = lookup.find(contig_name);
        if (ref_it == lookup.end()) throw std::logic_error("Pileup contig disappeared from reference");
        const auto& contig = reference[ref_it->second];
        for (const auto& [zero_based_position, site] : sites) {
            ++stats.sites_with_observations;
            const auto depth = site.counts[0] + site.counts[1] + site.counts[2] + site.counts[3];
            if (depth < options.minimum_depth) continue;
            ++stats.sites_meeting_minimum_depth;
            const auto ref_index = canonical_index(contig.sequence[static_cast<std::size_t>(zero_based_position)]);
            if (!ref_index.has_value()) continue;
            VariantSite call;
            call.contig = contig_name;
            call.position = zero_based_position + 1U;
            call.reference = canonical_base(*ref_index);
            call.depth = depth;
            for (std::size_t allele_index = 0U; allele_index < 4U; ++allele_index) {
                if (allele_index == *ref_index) continue;
                const auto count = site.counts[allele_index];
                if (count < options.minimum_alt_count) continue;
                const double fraction = static_cast<double>(count) / static_cast<double>(depth);
                if (fraction + std::numeric_limits<double>::epsilon() < options.minimum_alt_fraction) continue;
                call.alternates.push_back(VariantAllele{
                    canonical_base(allele_index),
                    count,
                    fraction,
                    count == 0U ? 0.0 : static_cast<double>(site.quality_sums[allele_index]) / static_cast<double>(count)
                });
            }
            if (!call.alternates.empty()) {
                stats.called_alt_alleles += call.alternates.size();
                stats.variants.push_back(std::move(call));
            }
        }
    }
    stats.called_sites = stats.variants.size();
}

[[nodiscard]] std::string json_escape(const std::string_view value) {
    std::string out;
    for (const char raw : value) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20U) {
                    std::ostringstream hex;
                    hex << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned int>(c);
                    out += hex.str();
                } else out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

class GzipInputBuffer final : public std::streambuf {
public:
    explicit GzipInputBuffer(const std::filesystem::path& path) : file_{path, std::ios::binary} {
        if (!file_) throw std::runtime_error("Unable to open BAM input");
        std::memset(&stream_, 0, sizeof(stream_));
        if (inflateInit2(&stream_, 15 + 16) != Z_OK) throw std::runtime_error("Unable to initialize zlib inflater");
        initialized_ = true;
        setg(output_.data(), output_.data(), output_.data());
    }

    ~GzipInputBuffer() override {
        if (initialized_) static_cast<void>(inflateEnd(&stream_));
    }

protected:
    int_type underflow() override {
        if (gptr() < egptr()) return traits_type::to_int_type(*gptr());
        if (finished_) return traits_type::eof();
        for (;;) {
            if (stream_.avail_in == 0U && !physical_eof_) {
                file_.read(input_.data(), static_cast<std::streamsize>(input_.size()));
                const auto count = file_.gcount();
                if (file_.bad()) throw std::runtime_error("Unable to read BAM compressed input");
                if (count <= 0) physical_eof_ = true;
                else {
                    stream_.next_in = reinterpret_cast<Bytef*>(input_.data());
                    stream_.avail_in = static_cast<uInt>(count);
                }
            }
            stream_.next_out = reinterpret_cast<Bytef*>(output_.data());
            stream_.avail_out = static_cast<uInt>(output_.size());
            const int result = inflate(&stream_, Z_NO_FLUSH);
            const auto produced = output_.size() - stream_.avail_out;
            if (result == Z_STREAM_END) {
                saw_complete_member_ = true;
                const auto remaining = stream_.avail_in;
                auto* remaining_ptr = stream_.next_in;
                if (inflateReset2(&stream_, 15 + 16) != Z_OK) throw std::runtime_error("Unable to reset zlib inflater");
                stream_.avail_in = remaining;
                stream_.next_in = remaining_ptr;
                between_members_ = true;
            } else if (result == Z_OK) {
                between_members_ = false;
            } else if (result == Z_BUF_ERROR && physical_eof_ && stream_.avail_in == 0U && between_members_ && saw_complete_member_) {
                finished_ = true;
            } else if (result != Z_BUF_ERROR) {
                throw std::invalid_argument("BAM gzip/BGZF stream is corrupt or truncated");
            }
            if (produced != 0U) {
                setg(output_.data(), output_.data(), output_.data() + static_cast<std::ptrdiff_t>(produced));
                return traits_type::to_int_type(*gptr());
            }
            if (finished_) return traits_type::eof();
            if (physical_eof_ && stream_.avail_in == 0U) {
                if (!between_members_ || !saw_complete_member_) {
                    throw std::invalid_argument("BAM gzip/BGZF stream ended before a complete trailer");
                }
                finished_ = true;
                return traits_type::eof();
            }
        }
    }

private:
    std::ifstream file_;
    z_stream stream_{};
    bool initialized_{false};
    bool physical_eof_{false};
    bool finished_{false};
    bool saw_complete_member_{false};
    bool between_members_{false};
    std::array<char, 64U * 1024U> input_{};
    std::array<char, 64U * 1024U> output_{};
};

class GzipInputStream final : public std::istream {
public:
    explicit GzipInputStream(const std::filesystem::path& path) : std::istream{nullptr}, buffer_{path} { rdbuf(&buffer_); }
private:
    GzipInputBuffer buffer_;
};

[[nodiscard]] bool gzip_magic(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("Unable to inspect alignment input");
    unsigned char bytes[2]{};
    input.read(reinterpret_cast<char*>(bytes), 2);
    return input.gcount() == 2 && bytes[0] == 0x1FU && bytes[1] == 0x8BU;
}

void read_exact(std::istream& input, void* destination, const std::size_t bytes, const std::string_view label) {
    input.read(static_cast<char*>(destination), static_cast<std::streamsize>(bytes));
    if (input.gcount() != static_cast<std::streamsize>(bytes)) {
        throw std::invalid_argument("BAM is truncated while reading " + std::string{label});
    }
}

[[nodiscard]] std::uint32_t le_u32(const std::uint8_t* data) noexcept {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

[[nodiscard]] std::int32_t le_i32(const std::uint8_t* data) noexcept {
    return std::bit_cast<std::int32_t>(le_u32(data));
}

[[nodiscard]] std::int32_t read_i32(std::istream& input, const std::string_view label) {
    std::array<std::uint8_t, 4> bytes{};
    read_exact(input, bytes.data(), bytes.size(), label);
    return le_i32(bytes.data());
}

void require_bytes(const std::size_t offset, const std::size_t count, const std::size_t total, const std::string_view label) {
    if (offset > total || count > total - offset) throw std::invalid_argument("BAM " + std::string{label} + " exceeds record bounds");
}

[[nodiscard]] std::vector<CigarOperation> parse_bam_cigar(
    const std::vector<std::uint8_t>& block,
    const std::size_t offset,
    const std::uint16_t count
) {
    static constexpr std::array<char, 9> operations{'M', 'I', 'D', 'N', 'S', 'H', 'P', '=', 'X'};
    std::vector<CigarOperation> result;
    result.reserve(count);
    for (std::uint16_t i = 0U; i < count; ++i) {
        const std::uint32_t encoded = le_u32(block.data() + offset + static_cast<std::size_t>(i) * 4U);
        const auto length = static_cast<std::uint64_t>(encoded >> 4U);
        const auto op = static_cast<std::size_t>(encoded & 0xFU);
        if (length == 0U || op >= operations.size()) throw std::invalid_argument("BAM CIGAR operation is invalid");
        result.push_back({length, operations[op]});
    }
    if (result.empty()) throw std::invalid_argument("Mapped BAM record has empty CIGAR");
    return result;
}

[[nodiscard]] std::string decode_bam_sequence(
    const std::vector<std::uint8_t>& block,
    const std::size_t offset,
    const std::size_t length
) {
    static constexpr std::array<char, 16> bases{'=', 'A', 'C', 'M', 'G', 'R', 'S', 'V', 'T', 'W', 'Y', 'H', 'K', 'D', 'B', 'N'};
    std::string sequence;
    sequence.resize(length);
    for (std::size_t i = 0U; i < length; ++i) {
        const auto packed = block[offset + i / 2U];
        const auto code = static_cast<std::size_t>((i % 2U == 0U ? packed >> 4U : packed) & 0x0FU);
        sequence[i] = bases[code];
    }
    return sequence;
}

[[nodiscard]] std::vector<std::uint8_t> decode_bam_qualities(
    const std::vector<std::uint8_t>& block,
    const std::size_t offset,
    const std::size_t length
) {
    std::vector<std::uint8_t> qualities(length);
    for (std::size_t i = 0U; i < length; ++i) {
        const auto value = block[offset + i];
        qualities[i] = value;
    }
    return qualities;
}

void validate_bam_reference_dictionary(
    const std::vector<std::pair<std::string, std::uint64_t>>& bam_references,
    const std::vector<ReferenceContig>& reference
) {
    if (bam_references.size() != reference.size()) {
        throw std::invalid_argument("BAM reference dictionary does not match reference FASTA");
    }
    for (std::size_t i = 0U; i < reference.size(); ++i) {
        if (bam_references[i].first != reference[i].name || bam_references[i].second != reference[i].sequence.size()) {
            throw std::invalid_argument("BAM reference dictionary does not match reference FASTA");
        }
    }
}

}  // namespace

void validate_variant_calling_options(const VariantCallingOptions& options) {
    if (options.minimum_depth < 1U || options.minimum_depth > 1000000U ||
        options.minimum_alt_count < 1U || options.minimum_alt_count > 1000000U ||
        !std::isfinite(options.minimum_alt_fraction) || options.minimum_alt_fraction < 0.01 || options.minimum_alt_fraction > 1.0 ||
        options.minimum_mapq > 60U || options.minimum_base_quality > 60U) {
        throw std::invalid_argument("Variant-calling options are outside supported bounds");
    }
}

std::vector<ReferenceContig> read_reference_fasta(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        throw std::invalid_argument("Variant reference FASTA must be a regular non-symlink file");
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0U || size > maximum_reference_file_bytes) {
        throw std::invalid_argument("Variant reference FASTA size is outside the safety bound");
    }
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("Unable to open variant reference FASTA");

    std::vector<ReferenceContig> contigs;
    std::unordered_set<std::string> names;
    std::string line;
    std::uint64_t total_bases = 0U;
    bool first_line = true;
    while (read_bounded_line(input, line, maximum_reference_line_bytes, "reference FASTA")) {
        if (first_line) {
            strip_utf8_bom(line);
            first_line = false;
        }
        if (line.empty()) continue;
        if (line.front() == '>') {
            const auto name = first_token(std::string_view{line}.substr(1U));
            if (name.empty() || name.size() > maximum_contig_name_bytes || name.find_first_of("\\\r\n") != std::string::npos) {
                throw std::invalid_argument("Variant reference FASTA contig identifier is invalid");
            }
            if (!names.insert(name).second) throw std::invalid_argument("Variant reference FASTA contains duplicate contigs");
            contigs.push_back({name, {}});
            continue;
        }
        if (contigs.empty()) throw std::invalid_argument("Reference FASTA sequence appears before header");
        for (char& base : line) {
            if (!valid_iupac(base)) throw std::invalid_argument("Variant reference FASTA contains non-IUPAC DNA");
            base = uppercase_ascii(base == 'U' ? 'T' : base);
        }
        if (line.size() > maximum_reference_bases - total_bases) throw std::invalid_argument("Variant reference FASTA exceeds base-count safety bound");
        total_bases += line.size();
        contigs.back().sequence += line;
    }
    if (contigs.empty() || total_bases == 0U) throw std::invalid_argument("Variant reference FASTA contains no sequence");
    for (const auto& contig : contigs) if (contig.sequence.empty()) throw std::invalid_argument("Variant reference FASTA contains empty contig");
    return contigs;
}

VariantCallingStatistics call_variants_from_sam(
    std::istream& input,
    const std::vector<ReferenceContig>& reference,
    const VariantCallingOptions& options
) {
    validate_variant_calling_options(options);
    VariantCallingStatistics stats;
    stats.alignment_format = "sam";
    Pileup pileup;
    std::size_t pileup_sites = 0U;
    const auto lookup = reference_lookup(reference);
    std::map<std::string, std::uint64_t> header_lengths;
    bool saw_record = false;
    bool first_line = true;
    bool alignment_started = false;
    std::string line;
    while (read_bounded_line(input, line, maximum_sam_line_bytes, "SAM")) {
        if (first_line) {
            strip_utf8_bom(line);
            first_line = false;
        }
        if (line.empty()) continue;
        if (line.front() == '@') {
            if (alignment_started) throw std::invalid_argument("SAM header appears after alignment records");
            const auto fields = split_tabs(line);
            if (!fields.empty() && fields[0] == "@SQ") {
                std::optional<std::string> name;
                std::optional<std::uint64_t> length;
                for (std::size_t i = 1U; i < fields.size(); ++i) {
                    if (fields[i].substr(0U, std::min<std::size_t>(3U, fields[i].size())) == "SN:") {
                        if (name.has_value() || fields[i].size() <= 3U) throw std::invalid_argument("SAM @SQ SN is invalid");
                        name = std::string{fields[i].substr(3U)};
                    } else if (fields[i].substr(0U, std::min<std::size_t>(3U, fields[i].size())) == "LN:") {
                        if (length.has_value()) throw std::invalid_argument("SAM @SQ LN is duplicated");
                        length = parse_unsigned<std::uint64_t>(fields[i].substr(3U), "SAM @SQ LN");
                        if (*length == 0U) throw std::invalid_argument("SAM @SQ LN must be positive");
                    }
                }
                if (!name.has_value() || !length.has_value() || !header_lengths.emplace(*name, *length).second) {
                    throw std::invalid_argument("SAM @SQ record is incomplete or duplicated");
                }
                const auto found = lookup.find(*name);
                if (found == lookup.end() || reference[found->second].sequence.size() != *length) {
                    throw std::invalid_argument("SAM @SQ dictionary does not match reference FASTA");
                }
            }
            continue;
        }
        alignment_started = true;
        saw_record = true;
        const auto fields = split_tabs(line);
        if (fields.size() < 11U) throw std::invalid_argument("SAM alignment requires 11 mandatory fields");
        const auto flag = parse_unsigned<std::uint16_t>(fields[1], "SAM FLAG");
        const auto mapq = parse_unsigned<std::uint32_t>(fields[4], "SAM MAPQ");
        if (mapq > 255U) throw std::invalid_argument("SAM MAPQ is outside 0..255");
        if ((flag & flag_unmapped) != 0U) {
            observe_alignment(stats, pileup, pileup_sites, reference.front(), 0U, flag, mapq, {}, {}, {}, options);
            continue;
        }
        const auto found = lookup.find(std::string{fields[2]});
        if (found == lookup.end()) throw std::invalid_argument("Mapped SAM contig is absent from reference FASTA");
        const auto position = parse_unsigned<std::uint64_t>(fields[3], "SAM POS");
        if (position == 0U) throw std::invalid_argument("Mapped SAM POS must be positive");
        const auto operations = parse_cigar(fields[5]);
        if (fields[9] == "*" || fields[10] == "*") throw std::invalid_argument("Mapped SAM record requires SEQ and QUAL for variant calling");
        if (fields[9].size() != fields[10].size()) throw std::invalid_argument("SAM SEQ and QUAL lengths differ");
        std::string sequence{fields[9]};
        std::vector<std::uint8_t> qualities;
        qualities.reserve(fields[10].size());
        for (char& base : sequence) {
            if (base != '=' && !valid_iupac(base)) throw std::invalid_argument("SAM sequence contains non-IUPAC DNA");
            if (base != '=') base = uppercase_ascii(base == 'U' ? 'T' : base);
        }
        for (const char quality : fields[10]) {
            const auto encoded = static_cast<unsigned char>(quality);
            if (encoded < 33U || encoded > 126U) throw std::invalid_argument("SAM QUAL is outside Phred+33 range");
            qualities.push_back(static_cast<std::uint8_t>(encoded - 33U));
        }
        observe_alignment(stats, pileup, pileup_sites, reference[found->second], position - 1U, flag, mapq, operations, sequence, qualities, options);
    }
    if (!saw_record) throw std::invalid_argument("SAM contains no alignment records");
    if (!header_lengths.empty() && header_lengths.size() != reference.size()) {
        throw std::invalid_argument("SAM @SQ dictionary is incomplete for the supplied reference FASTA");
    }
    finalize_calls(stats, pileup, reference, options);
    return stats;
}

VariantCallingStatistics call_variants_from_bam(
    std::istream& input,
    const std::vector<ReferenceContig>& reference,
    const VariantCallingOptions& options
) {
    validate_variant_calling_options(options);
    std::array<char, 4> magic{};
    read_exact(input, magic.data(), magic.size(), "BAM magic");
    if (std::string_view{magic.data(), magic.size()} != std::string_view{"BAM\1", 4U}) throw std::invalid_argument("BAM magic is invalid");
    const auto header_length = read_i32(input, "header text length");
    if (header_length < 0 || static_cast<std::uint32_t>(header_length) > maximum_bam_header_text_bytes) throw std::invalid_argument("BAM header text length is invalid");
    std::vector<char> header(static_cast<std::size_t>(header_length));
    if (!header.empty()) read_exact(input, header.data(), header.size(), "header text");
    const auto reference_count = read_i32(input, "reference count");
    if (reference_count <= 0 || static_cast<std::uint32_t>(reference_count) > maximum_bam_reference_count) throw std::invalid_argument("BAM reference count is invalid");
    std::vector<std::pair<std::string, std::uint64_t>> bam_references;
    bam_references.reserve(static_cast<std::size_t>(reference_count));
    std::unordered_set<std::string> names;
    for (std::int32_t i = 0; i < reference_count; ++i) {
        const auto name_length = read_i32(input, "reference name length");
        if (name_length < 2 || static_cast<std::uint32_t>(name_length) > maximum_bam_reference_name_bytes) throw std::invalid_argument("BAM reference name length is invalid");
        std::vector<char> name(static_cast<std::size_t>(name_length));
        read_exact(input, name.data(), name.size(), "reference name");
        if (name.back() != '\0') throw std::invalid_argument("BAM reference name is not NUL terminated");
        name.pop_back();
        const auto length = read_i32(input, "reference length");
        if (length <= 0) throw std::invalid_argument("BAM reference length is invalid");
        std::string contig{name.begin(), name.end()};
        if (!names.insert(contig).second) throw std::invalid_argument("BAM contains duplicate reference names");
        bam_references.emplace_back(std::move(contig), static_cast<std::uint64_t>(length));
    }
    validate_bam_reference_dictionary(bam_references, reference);

    VariantCallingStatistics stats;
    stats.alignment_format = "bam";
    Pileup pileup;
    std::size_t pileup_sites = 0U;
    bool saw_record = false;
    for (;;) {
        std::array<std::uint8_t, 4> size_bytes{};
        input.read(reinterpret_cast<char*>(size_bytes.data()), static_cast<std::streamsize>(size_bytes.size()));
        const auto got = input.gcount();
        if (got == 0 && input.eof()) break;
        if (got != static_cast<std::streamsize>(size_bytes.size())) throw std::invalid_argument("BAM is truncated at record size");
        const auto block_size = le_i32(size_bytes.data());
        if (block_size < 32 || block_size > maximum_bam_record_bytes) throw std::invalid_argument("BAM record block size is invalid");
        std::vector<std::uint8_t> block(static_cast<std::size_t>(block_size));
        read_exact(input, block.data(), block.size(), "alignment record");
        saw_record = true;
        const auto ref_id = le_i32(block.data());
        const auto pos = le_i32(block.data() + 4U);
        const auto bin_mq_nl = le_u32(block.data() + 8U);
        const auto flag_nc = le_u32(block.data() + 12U);
        const auto l_seq = le_i32(block.data() + 16U);
        const auto l_read_name = static_cast<std::uint8_t>(bin_mq_nl & 0xFFU);
        const auto mapq = static_cast<std::uint32_t>((bin_mq_nl >> 8U) & 0xFFU);
        const auto n_cigar = static_cast<std::uint16_t>(flag_nc & 0xFFFFU);
        const auto flag = static_cast<std::uint16_t>((flag_nc >> 16U) & 0xFFFFU);
        if (l_seq < 0 || l_read_name < 2U) throw std::invalid_argument("BAM alignment core fields are invalid");
        std::size_t offset = 32U;
        require_bytes(offset, l_read_name, block.size(), "read name");
        if (block[offset + l_read_name - 1U] != 0U) throw std::invalid_argument("BAM read name is not NUL terminated");
        offset += l_read_name;
        const auto cigar_offset = offset;
        const auto cigar_bytes = static_cast<std::size_t>(n_cigar) * 4U;
        require_bytes(offset, cigar_bytes, block.size(), "CIGAR");
        offset += cigar_bytes;
        const auto sequence_offset = offset;
        const auto sequence_bytes = (static_cast<std::size_t>(l_seq) + 1U) / 2U;
        require_bytes(offset, sequence_bytes, block.size(), "sequence");
        offset += sequence_bytes;
        const auto quality_offset = offset;
        require_bytes(offset, static_cast<std::size_t>(l_seq), block.size(), "quality");
        offset += static_cast<std::size_t>(l_seq);
        static_cast<void>(offset);  // Aux tags are irrelevant to SNV pileup in Iteration 037.

        if ((flag & flag_unmapped) != 0U) {
            observe_alignment(stats, pileup, pileup_sites, reference.front(), 0U, flag, mapq, {}, {}, {}, options);
            continue;
        }
        if (ref_id < 0 || ref_id >= reference_count || pos < 0) throw std::invalid_argument("Mapped BAM record has invalid reference id/position");
        if (n_cigar == 0U) throw std::invalid_argument("Mapped BAM record requires a CIGAR");
        const auto operations = parse_bam_cigar(block, cigar_offset, n_cigar);
        const auto sequence = decode_bam_sequence(block, sequence_offset, static_cast<std::size_t>(l_seq));
        const auto qualities = decode_bam_qualities(block, quality_offset, static_cast<std::size_t>(l_seq));
        observe_alignment(stats, pileup, pileup_sites, reference[static_cast<std::size_t>(ref_id)], static_cast<std::uint64_t>(pos), flag, mapq, operations, sequence, qualities, options);
    }
    if (!saw_record) throw std::invalid_argument("BAM contains no alignment records");
    finalize_calls(stats, pileup, reference, options);
    return stats;
}

VariantCallingStatistics call_variants_from_alignment_file(
    const std::filesystem::path& path,
    const std::string_view file_type,
    const std::vector<ReferenceContig>& reference,
    const VariantCallingOptions& options
) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) throw std::invalid_argument("Variant alignment input must be a regular non-symlink file");
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0U) throw std::invalid_argument("Variant alignment input must be non-empty");
    if (file_type == "sam") {
        if (gzip_magic(path)) throw std::invalid_argument("SAM variant input must be plain text");
        std::ifstream input{path, std::ios::binary};
        if (!input) throw std::runtime_error("Unable to open SAM variant input");
        return call_variants_from_sam(input, reference, options);
    }
    if (file_type == "bam") {
        if (!gzip_magic(path)) throw std::invalid_argument("BAM variant input must be BGZF/gzip encoded");
        GzipInputStream input{path};
        return call_variants_from_bam(input, reference, options);
    }
    throw std::invalid_argument("Variant alignment input file type must be sam or bam");
}

std::string render_vcf(
    const VariantCallingStatistics& statistics,
    const std::vector<ReferenceContig>& reference,
    const VariantCallingOptions& options
) {
    std::ostringstream out;
    out << "##fileformat=VCFv4.3\n"
        << "##source=OpenGenesis-BioCore-native-snv-pileup-v1\n"
        << "##BioCoreMinDepth=" << options.minimum_depth << "\n"
        << "##BioCoreMinAltCount=" << options.minimum_alt_count << "\n"
        << std::fixed << std::setprecision(6)
        << "##BioCoreMinAltFraction=" << options.minimum_alt_fraction << "\n"
        << "##BioCoreMinMAPQ=" << options.minimum_mapq << "\n"
        << "##BioCoreMinBaseQuality=" << options.minimum_base_quality << "\n";
    for (const auto& contig : reference) {
        out << "##contig=<ID=" << contig.name << ",length=" << contig.sequence.size() << ">\n";
    }
    out << "##INFO=<ID=DP,Number=1,Type=Integer,Description=High-quality canonical depth>\n"
        << "##INFO=<ID=AC,Number=A,Type=Integer,Description=Alternate allele observation count>\n"
        << "##INFO=<ID=AF,Number=A,Type=Float,Description=Alternate allele fraction>\n"
        << "##INFO=<ID=ABQ,Number=A,Type=Float,Description=Average alternate base quality>\n"
        << "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\n";
    for (const auto& site : statistics.variants) {
        out << site.contig << '\t' << site.position << "\t.\t" << site.reference << '\t';
        for (std::size_t i = 0U; i < site.alternates.size(); ++i) {
            if (i != 0U) out << ',';
            out << site.alternates[i].base;
        }
        out << "\t.\tPASS\tDP=" << site.depth << ";AC=";
        for (std::size_t i = 0U; i < site.alternates.size(); ++i) {
            if (i != 0U) out << ',';
            out << site.alternates[i].count;
        }
        out << ";AF=";
        for (std::size_t i = 0U; i < site.alternates.size(); ++i) {
            if (i != 0U) out << ',';
            out << site.alternates[i].fraction;
        }
        out << ";ABQ=";
        for (std::size_t i = 0U; i < site.alternates.size(); ++i) {
            if (i != 0U) out << ',';
            out << site.alternates[i].average_base_quality;
        }
        out << '\n';
    }
    return out.str();
}

std::string render_variant_json(
    const VariantCallingStatistics& s,
    const VariantCallingOptions& options
) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6);
    out << "{\n  \"algorithm\": \"native-snv-pileup-v1\",\n"
        << "  \"alignmentFormat\": \"" << json_escape(s.alignment_format) << "\",\n"
        << "  \"parameters\": {\"minDepth\": " << options.minimum_depth
        << ", \"minAltCount\": " << options.minimum_alt_count
        << ", \"minAltFraction\": " << options.minimum_alt_fraction
        << ", \"minMapq\": " << options.minimum_mapq
        << ", \"minBaseQuality\": " << options.minimum_base_quality << "},\n"
        << "  \"totalRecords\": " << s.total_records << ",\n"
        << "  \"eligibleRecords\": " << s.eligible_records << ",\n"
        << "  \"unmappedRecords\": " << s.unmapped_records << ",\n"
        << "  \"secondaryRecords\": " << s.secondary_records << ",\n"
        << "  \"supplementaryRecords\": " << s.supplementary_records << ",\n"
        << "  \"duplicateRecords\": " << s.duplicate_records << ",\n"
        << "  \"qcFailedRecords\": " << s.qc_failed_records << ",\n"
        << "  \"lowMapqRecords\": " << s.low_mapq_records << ",\n"
        << "  \"unavailableMapqRecords\": " << s.unavailable_mapq_records << ",\n"
        << "  \"callableBaseObservations\": " << s.callable_base_observations << ",\n"
        << "  \"lowQualityBaseObservations\": " << s.low_quality_base_observations << ",\n"
        << "  \"ambiguousReadBaseObservations\": " << s.ambiguous_read_base_observations << ",\n"
        << "  \"noncanonicalReferenceObservations\": " << s.noncanonical_reference_observations << ",\n"
        << "  \"sitesWithObservations\": " << s.sites_with_observations << ",\n"
        << "  \"sitesMeetingMinimumDepth\": " << s.sites_meeting_minimum_depth << ",\n"
        << "  \"calledSites\": " << s.called_sites << ",\n"
        << "  \"calledAltAlleles\": " << s.called_alt_alleles << ",\n"
        << "  \"variants\": [";
    for (std::size_t i = 0U; i < s.variants.size(); ++i) {
        if (i != 0U) out << ',';
        const auto& site = s.variants[i];
        out << "{\"contig\":\"" << json_escape(site.contig) << "\",\"position\":" << site.position
            << ",\"reference\":\"" << site.reference << "\",\"depth\":" << site.depth << ",\"alternates\":[";
        for (std::size_t j = 0U; j < site.alternates.size(); ++j) {
            if (j != 0U) out << ',';
            const auto& alt = site.alternates[j];
            out << "{\"base\":\"" << alt.base << "\",\"count\":" << alt.count
                << ",\"fraction\":" << alt.fraction
                << ",\"averageBaseQuality\":" << alt.average_base_quality << '}';
        }
        out << "]}";
    }
    out << "]\n}\n";
    return out.str();
}

std::string render_variant_tsv(const VariantCallingStatistics& s) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6);
    out << "contig\tposition\treference\talternate\tdepth\talt_count\talt_fraction\taverage_alt_base_quality\n";
    for (const auto& site : s.variants) {
        for (const auto& alt : site.alternates) {
            out << site.contig << '\t' << site.position << '\t' << site.reference << '\t' << alt.base
                << '\t' << site.depth << '\t' << alt.count << '\t' << alt.fraction
                << '\t' << alt.average_base_quality << '\n';
        }
    }
    return out.str();
}

}  // namespace biocore::variant_calling
