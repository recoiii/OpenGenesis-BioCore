#include "alignment_qc.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
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
#include <unordered_set>
#include <utility>
#include <vector>

#include <zlib.h>

namespace biocore::alignment_qc {
namespace {

constexpr std::size_t maximum_sam_line_bytes = 64U * 1024U * 1024U;
constexpr std::uint32_t maximum_bam_header_text_bytes = 64U * 1024U * 1024U;
constexpr std::uint32_t maximum_bam_reference_count = 1000000U;
constexpr std::uint32_t maximum_bam_reference_name_bytes = 1024U * 1024U;
constexpr std::int32_t maximum_bam_record_bytes = 64 * 1024 * 1024;
constexpr std::uint64_t maximum_reference_length = 5000000000ULL;
constexpr std::uint16_t flag_paired = 0x1U;
constexpr std::uint16_t flag_proper_pair = 0x2U;
constexpr std::uint16_t flag_unmapped = 0x4U;
constexpr std::uint16_t flag_mate_unmapped = 0x8U;
constexpr std::uint16_t flag_reverse = 0x10U;
constexpr std::uint16_t flag_read1 = 0x40U;
constexpr std::uint16_t flag_read2 = 0x80U;
constexpr std::uint16_t flag_secondary = 0x100U;
constexpr std::uint16_t flag_qc_fail = 0x200U;
constexpr std::uint16_t flag_duplicate = 0x400U;
constexpr std::uint16_t flag_supplementary = 0x800U;

using CoverageEvents = std::map<std::string, std::vector<std::pair<std::uint64_t, std::int64_t>>>;

[[nodiscard]] bool read_bounded_line(std::istream& input, std::string& line) {
    line.clear();
    bool saw = false;
    for (;;) {
        const int next = input.get();
        if (next == std::char_traits<char>::eof()) {
            if (input.bad()) throw std::runtime_error("Unable to read SAM input");
            return saw;
        }
        saw = true;
        const char value = static_cast<char>(next);
        if (value == '\n') break;
        if (line.size() >= maximum_sam_line_bytes) {
            throw std::invalid_argument("SAM line exceeds the 64 MiB safety limit");
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

[[nodiscard]] std::vector<std::string_view> split_tabs(const std::string& line) {
    std::vector<std::string_view> fields;
    std::size_t begin = 0U;
    for (;;) {
        const auto end = line.find('\t', begin);
        fields.emplace_back(
            line.data() + begin,
            (end == std::string::npos ? line.size() : end) - begin
        );
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
    const auto* begin = value.data();
    const auto* end = begin + value.size();
    const auto result = std::from_chars(begin, end, parsed, 10);
    if (result.ec != std::errc{} || result.ptr != end ||
        parsed > std::numeric_limits<T>::max()) {
        throw std::invalid_argument(std::string{label} + " is invalid");
    }
    return static_cast<T>(parsed);
}

[[nodiscard]] std::int64_t parse_signed_i64(
    const std::string_view value, const std::string_view label
) {
    if (value.empty()) throw std::invalid_argument(std::string{label} + " is invalid");
    std::int64_t parsed = 0;
    const auto* begin = value.data();
    const auto* end = begin + value.size();
    const auto result = std::from_chars(begin, end, parsed, 10);
    if (result.ec != std::errc{} || result.ptr != end) {
        throw std::invalid_argument(std::string{label} + " is invalid");
    }
    return parsed;
}

[[nodiscard]] std::optional<std::uint64_t> sam_nm(
    const std::vector<std::string_view>& fields
) {
    std::optional<std::uint64_t> result;
    for (std::size_t i = 11U; i < fields.size(); ++i) {
        const auto field = fields[i];
        if (field.size() < 5U || field.substr(0U, 5U) != "NM:i:") continue;
        if (result.has_value()) {
            throw std::invalid_argument("SAM record contains duplicate NM tags");
        }
        result = parse_unsigned<std::uint64_t>(field.substr(5U), "SAM NM tag");
    }
    return result;
}

void observe_record(
    AlignmentQcStatistics& stats,
    const std::uint16_t flag,
    const std::uint32_t mapq,
    const std::string_view contig,
    const std::optional<std::uint64_t> nm
) {
    ++stats.total_records;
    if ((flag & flag_secondary) != 0U) ++stats.secondary_records;
    if ((flag & flag_supplementary) != 0U) ++stats.supplementary_records;
    if ((flag & flag_duplicate) != 0U) ++stats.duplicate_records;
    if ((flag & flag_qc_fail) != 0U) ++stats.qc_failed_records;

    const bool primary = (flag & (flag_secondary | flag_supplementary)) == 0U;
    if (!primary) return;
    ++stats.primary_records;
    const bool mapped = (flag & flag_unmapped) == 0U;
    if (mapped) {
        ++stats.primary_mapped;
        if ((flag & flag_reverse) != 0U) ++stats.reverse_primary_mapped;
        ++stats.mapq_observations;
        stats.mapq_sum += mapq;
        stats.minimum_mapq = std::min(stats.minimum_mapq, mapq);
        stats.maximum_mapq = std::max(stats.maximum_mapq, mapq);
        const std::size_t bin = mapq == 0U ? 0U : mapq < 10U ? 1U :
                                mapq < 20U ? 2U : mapq < 30U ? 3U :
                                mapq < 40U ? 4U : mapq < 60U ? 5U : 6U;
        ++stats.mapq_bins[bin];
        if (contig.empty() || contig == "*") {
            throw std::invalid_argument("Mapped alignment is missing a reference name");
        }
        ++stats.contig_primary_mapped[std::string{contig}];
        if (nm.has_value()) {
            ++stats.nm_observations;
            stats.nm_sum += *nm;
            stats.minimum_nm = std::min(stats.minimum_nm, *nm);
            stats.maximum_nm = std::max(stats.maximum_nm, *nm);
        }
    } else {
        ++stats.primary_unmapped;
    }
    if ((flag & flag_paired) != 0U) {
        ++stats.paired_primary_records;
        if ((flag & flag_proper_pair) != 0U) ++stats.proper_pair_primary_records;
        if ((flag & flag_mate_unmapped) != 0U) ++stats.mate_unmapped_primary_records;
        const bool read1 = (flag & flag_read1) != 0U;
        const bool read2 = (flag & flag_read2) != 0U;
        if (read1 && read2) {
            throw std::invalid_argument("Alignment record cannot be both read1 and read2");
        }
        if (read1) ++stats.read1_primary_records;
        if (read2) ++stats.read2_primary_records;
    }
}

void observe_template_length(
    AlignmentQcStatistics& stats,
    const std::uint16_t flag,
    const bool same_reference,
    const std::int64_t template_length
) {
    const bool primary = (flag & (flag_secondary | flag_supplementary)) == 0U;
    const bool eligible = primary && (flag & flag_paired) != 0U &&
                          (flag & flag_proper_pair) != 0U &&
                          (flag & flag_unmapped) == 0U &&
                          (flag & flag_mate_unmapped) == 0U && same_reference &&
                          template_length > 0;
    if (!eligible) return;
    const auto value = static_cast<std::uint64_t>(template_length);
    if (stats.template_length_sum > std::numeric_limits<std::uint64_t>::max() - value) {
        throw std::overflow_error("Template-length sum exceeds supported range");
    }
    ++stats.template_length_observations;
    stats.template_length_sum += value;
    stats.minimum_template_length = std::min(stats.minimum_template_length, value);
    stats.maximum_template_length = std::max(stats.maximum_template_length, value);
}

[[nodiscard]] std::vector<std::pair<std::uint64_t, char>> parse_sam_cigar(
    const std::string_view cigar
) {
    if (cigar.empty() || cigar == "*") {
        throw std::invalid_argument("Mapped SAM record is missing CIGAR");
    }
    std::vector<std::pair<std::uint64_t, char>> operations;
    std::size_t offset = 0U;
    while (offset < cigar.size()) {
        const auto begin = offset;
        while (offset < cigar.size() && cigar[offset] >= '0' && cigar[offset] <= '9') ++offset;
        if (begin == offset || offset == cigar.size()) {
            throw std::invalid_argument("SAM CIGAR is malformed");
        }
        const auto length = parse_unsigned<std::uint64_t>(cigar.substr(begin, offset - begin), "SAM CIGAR length");
        if (length == 0U) throw std::invalid_argument("SAM CIGAR operation length must be positive");
        const char op = cigar[offset++];
        switch (op) {
            case 'M': case 'I': case 'D': case 'N': case 'S': case 'H': case 'P': case '=': case 'X':
                break;
            default:
                throw std::invalid_argument("SAM CIGAR contains an unsupported operation");
        }
        operations.emplace_back(length, op);
    }
    return operations;
}

[[nodiscard]] std::vector<std::pair<std::uint64_t, char>> parse_bam_cigar(
    const std::vector<unsigned char>& block,
    const std::size_t offset,
    const std::uint16_t n_cigar
) {
    if (n_cigar == 0U) throw std::invalid_argument("Mapped BAM record is missing CIGAR");
    const auto bytes = static_cast<std::size_t>(n_cigar) * 4U;
    if (offset > block.size() || bytes > block.size() - offset) {
        throw std::invalid_argument("Malformed BAM CIGAR");
    }
    static constexpr std::array<char, 9> bam_ops{{'M','I','D','N','S','H','P','=','X'}};
    std::vector<std::pair<std::uint64_t, char>> operations;
    operations.reserve(n_cigar);
    for (std::uint16_t i = 0U; i < n_cigar; ++i) {
        const auto value = static_cast<std::uint32_t>(block[offset + static_cast<std::size_t>(i) * 4U]) |
            (static_cast<std::uint32_t>(block[offset + static_cast<std::size_t>(i) * 4U + 1U]) << 8U) |
            (static_cast<std::uint32_t>(block[offset + static_cast<std::size_t>(i) * 4U + 2U]) << 16U) |
            (static_cast<std::uint32_t>(block[offset + static_cast<std::size_t>(i) * 4U + 3U]) << 24U);
        const auto length = static_cast<std::uint64_t>(value >> 4U);
        const auto code = static_cast<std::uint8_t>(value & 0xFU);
        if (length == 0U || code >= bam_ops.size()) {
            throw std::invalid_argument("BAM CIGAR operation is invalid");
        }
        operations.emplace_back(length, bam_ops[code]);
    }
    return operations;
}

void add_coverage_interval(
    CoverageEvents& events,
    const std::string_view contig,
    const std::uint64_t begin,
    const std::uint64_t end
) {
    if (begin >= end) return;
    auto& contig_events = events[std::string{contig}];
    contig_events.emplace_back(begin, 1);
    contig_events.emplace_back(end, -1);
}

void observe_coverage(
    CoverageEvents& events,
    const std::map<std::string, std::uint64_t>& reference_lengths,
    const std::uint16_t flag,
    const std::string_view contig,
    const std::uint64_t start_zero_based,
    const std::vector<std::pair<std::uint64_t, char>>& operations,
    const bool coverage_available
) {
    const bool primary_mapped = (flag & (flag_secondary | flag_supplementary | flag_unmapped)) == 0U;
    std::uint64_t reference_position = start_zero_based;
    for (const auto& [length, op] : operations) {
        const bool consumes_reference = op == 'M' || op == 'D' || op == 'N' || op == '=' || op == 'X';
        if (consumes_reference && reference_position > std::numeric_limits<std::uint64_t>::max() - length) {
            throw std::overflow_error("CIGAR reference span exceeds supported range");
        }
        if (op == 'M' || op == '=' || op == 'X') {
            const auto end = reference_position + length;
            if (primary_mapped && coverage_available) {
                add_coverage_interval(events, contig, reference_position, end);
            }
            reference_position = end;
        } else if (op == 'D' || op == 'N') {
            reference_position += length;
        }
    }
    if (coverage_available) {
        const auto found = reference_lengths.find(std::string{contig});
        if (found == reference_lengths.end()) {
            throw std::invalid_argument("Alignment references a contig without a declared length");
        }
        if (reference_position > found->second) {
            throw std::invalid_argument("Alignment CIGAR extends beyond the declared reference length");
        }
    }
}

void add_checked_product(
    std::uint64_t& target,
    const std::uint64_t left,
    const std::uint64_t right,
    const char* label
) {
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left) {
        throw std::overflow_error(label);
    }
    const auto value = left * right;
    if (target > std::numeric_limits<std::uint64_t>::max() - value) {
        throw std::overflow_error(label);
    }
    target += value;
}

void finalize_coverage(
    AlignmentQcStatistics& stats,
    const std::map<std::string, std::uint64_t>& reference_lengths,
    CoverageEvents& events
) {
    stats.coverage_available = !reference_lengths.empty();
    if (!stats.coverage_available) return;

    for (const auto& [name, reference_length] : reference_lengths) {
        if (stats.total_reference_bases > std::numeric_limits<std::uint64_t>::max() - reference_length) {
            throw std::overflow_error("Total reference length exceeds supported range");
        }
        stats.total_reference_bases += reference_length;
        ContigCoverageStatistics contig_stats;
        contig_stats.reference_length = reference_length;

        auto& contig_events = events[name];
        std::sort(contig_events.begin(), contig_events.end(), [](const auto& left, const auto& right) {
            return left.first < right.first || (left.first == right.first && left.second < right.second);
        });

        std::uint64_t previous = 0U;
        std::int64_t depth = 0;
        std::size_t index = 0U;
        const auto accumulate_segment = [&](const std::uint64_t end) {
            if (end < previous || end > reference_length) {
                throw std::logic_error("Coverage sweep produced an invalid segment");
            }
            const auto length = end - previous;
            if (length == 0U || depth == 0) return;
            if (depth < 0) throw std::logic_error("Coverage depth became negative");
            const auto unsigned_depth = static_cast<std::uint64_t>(depth);
            contig_stats.covered_bases += length;
            contig_stats.bases_at_least_1x += length;
            if (unsigned_depth >= 10U) contig_stats.bases_at_least_10x += length;
            if (unsigned_depth >= 20U) contig_stats.bases_at_least_20x += length;
            if (unsigned_depth >= 30U) contig_stats.bases_at_least_30x += length;
            contig_stats.maximum_depth = std::max(contig_stats.maximum_depth, unsigned_depth);
            add_checked_product(contig_stats.total_depth, length, unsigned_depth, "Coverage depth sum exceeds supported range");
        };

        while (index < contig_events.size()) {
            const auto position = contig_events[index].first;
            if (position > reference_length) {
                throw std::logic_error("Coverage event exceeds declared reference length");
            }
            accumulate_segment(position);
            previous = position;
            std::int64_t delta = 0;
            while (index < contig_events.size() && contig_events[index].first == position) {
                delta += contig_events[index].second;
                ++index;
            }
            depth += delta;
            if (depth < 0) throw std::logic_error("Coverage event balance became negative");
        }
        accumulate_segment(reference_length);
        if (depth != 0) throw std::logic_error("Coverage event balance did not return to zero");

        stats.covered_reference_bases += contig_stats.covered_bases;
        if (stats.total_depth > std::numeric_limits<std::uint64_t>::max() - contig_stats.total_depth) {
            throw std::overflow_error("Total coverage depth exceeds supported range");
        }
        stats.total_depth += contig_stats.total_depth;
        stats.maximum_depth = std::max(stats.maximum_depth, contig_stats.maximum_depth);
        stats.bases_at_least_1x += contig_stats.bases_at_least_1x;
        stats.bases_at_least_10x += contig_stats.bases_at_least_10x;
        stats.bases_at_least_20x += contig_stats.bases_at_least_20x;
        stats.bases_at_least_30x += contig_stats.bases_at_least_30x;
        stats.contig_coverage.emplace(name, contig_stats);
    }
}

[[nodiscard]] std::string json_escape(const std::string_view value) {
    std::ostringstream out;
    for (const char raw : value) {
        const auto c = static_cast<unsigned char>(raw);
        switch (c) {
            case '\\': out << "\\\\"; break;
            case '"': out << "\\\""; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20U) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<unsigned>(c) << std::dec;
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    return out.str();
}

[[nodiscard]] double percentage(
    const std::uint64_t numerator, const std::uint64_t denominator
) noexcept {
    return denominator == 0U ? 0.0 :
        (100.0 * static_cast<double>(numerator) / static_cast<double>(denominator));
}

[[nodiscard]] double average(
    const std::uint64_t sum, const std::uint64_t count
) noexcept {
    return count == 0U ? 0.0 :
        static_cast<double>(sum) / static_cast<double>(count);
}

class GzipStreamBuffer final : public std::streambuf {
public:
    explicit GzipStreamBuffer(const std::filesystem::path& path)
        : input_{path, std::ios::binary} {
        if (!input_) throw std::runtime_error("Unable to open BAM input");
        stream_.zalloc = Z_NULL;
        stream_.zfree = Z_NULL;
        stream_.opaque = Z_NULL;
        if (inflateInit2(&stream_, 16 + MAX_WBITS) != Z_OK) {
            throw std::runtime_error("Unable to initialize BAM BGZF/gzip decompressor");
        }
        initialized_ = true;
        setg(output_.data(), output_.data(), output_.data());
    }

    ~GzipStreamBuffer() override {
        if (initialized_) (void)inflateEnd(&stream_);
    }

    GzipStreamBuffer(const GzipStreamBuffer&) = delete;
    GzipStreamBuffer& operator=(const GzipStreamBuffer&) = delete;

protected:
    [[nodiscard]] int_type underflow() override {
        if (gptr() < egptr()) return traits_type::to_int_type(*gptr());
        for (;;) {
            if (member_finished_) {
                const Bytef* leftover = stream_.next_in;
                const uInt leftover_size = stream_.avail_in;
                if (inflateReset2(&stream_, 16 + MAX_WBITS) != Z_OK) {
                    throw std::runtime_error("Unable to reset BAM BGZF/gzip decompressor");
                }
                stream_.next_in = const_cast<Bytef*>(leftover);
                stream_.avail_in = leftover_size;
                member_finished_ = false;
            }
            fill_input();
            if (stream_.avail_in == 0U && input_eof_) {
                if (member_finished_) continue;
                if (stream_.total_in == 0U) return traits_type::eof();
                throw std::invalid_argument("BAM BGZF/gzip input ended before a complete member trailer");
            }
            stream_.next_out = reinterpret_cast<Bytef*>(output_.data());
            stream_.avail_out = static_cast<uInt>(output_.size());
            const auto input_before = stream_.avail_in;
            const auto output_before = stream_.avail_out;
            const int code = inflate(&stream_, Z_NO_FLUSH);
            const auto produced = static_cast<std::size_t>(output_before - stream_.avail_out);
            if (code == Z_STREAM_END) member_finished_ = true;
            else if (code != Z_OK && code != Z_BUF_ERROR) {
                std::string message{"Unable to decompress BAM BGZF/gzip input"};
                if (stream_.msg != nullptr && *stream_.msg != '\0') {
                    message += ": ";
                    message += stream_.msg;
                }
                throw std::runtime_error(message);
            }
            if (produced != 0U) {
                setg(output_.data(), output_.data(), output_.data() + produced);
                return traits_type::to_int_type(*gptr());
            }
            if (code == Z_BUF_ERROR && input_before == stream_.avail_in &&
                output_before == stream_.avail_out && stream_.avail_in != 0U) {
                throw std::runtime_error("BAM BGZF/gzip decompressor made no progress");
            }
        }
    }

private:
    void fill_input() {
        if (stream_.avail_in != 0U || input_eof_) return;
        input_.read(
            reinterpret_cast<char*>(compressed_.data()),
            static_cast<std::streamsize>(compressed_.size())
        );
        const auto count = input_.gcount();
        if (input_.bad()) throw std::runtime_error("Unable to read BAM input");
        if (count == 0) {
            input_eof_ = true;
            return;
        }
        stream_.next_in = compressed_.data();
        stream_.avail_in = static_cast<uInt>(count);
    }

    std::ifstream input_;
    z_stream stream_{};
    std::array<unsigned char, 64U * 1024U> compressed_{};
    std::array<char, 64U * 1024U> output_{};
    bool initialized_{false};
    bool input_eof_{false};
    bool member_finished_{false};
};

class GzipInputStream final : public std::istream {
public:
    explicit GzipInputStream(const std::filesystem::path& path)
        : std::istream{nullptr}, buffer_{path} {
        rdbuf(&buffer_);
    }
private:
    GzipStreamBuffer buffer_;
};

void read_exact(
    std::istream& input,
    void* destination,
    const std::size_t bytes,
    const char* label
) {
    if (bytes == 0U) return;
    input.read(static_cast<char*>(destination), static_cast<std::streamsize>(bytes));
    if (input.gcount() != static_cast<std::streamsize>(bytes)) {
        throw std::invalid_argument(std::string{"BAM ended while reading "} + label);
    }
}

[[nodiscard]] std::uint32_t le_u32(const unsigned char* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8U) |
           (static_cast<std::uint32_t>(p[2]) << 16U) |
           (static_cast<std::uint32_t>(p[3]) << 24U);
}

[[nodiscard]] std::int32_t le_i32(const unsigned char* p) noexcept {
    return std::bit_cast<std::int32_t>(le_u32(p));
}

[[nodiscard]] std::uint16_t le_u16(const unsigned char* p) noexcept {
    return static_cast<std::uint16_t>(p[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[1]) << 8U);
}

[[nodiscard]] std::int16_t le_i16(const unsigned char* p) noexcept {
    return std::bit_cast<std::int16_t>(le_u16(p));
}

[[nodiscard]] std::uint32_t read_le_u32(std::istream& input, const char* label) {
    unsigned char b[4]{};
    read_exact(input, b, sizeof(b), label);
    return le_u32(b);
}

[[nodiscard]] std::int32_t read_le_i32(std::istream& input, const char* label) {
    return std::bit_cast<std::int32_t>(read_le_u32(input, label));
}

void require_bytes(
    const std::size_t offset,
    const std::size_t count,
    const std::size_t size,
    const char* label
) {
    if (offset > size || count > size - offset) {
        throw std::invalid_argument(std::string{"Malformed BAM "} + label);
    }
}

[[nodiscard]] std::optional<std::uint64_t> bam_nm(
    const std::vector<unsigned char>& block,
    std::size_t offset
) {
    std::optional<std::uint64_t> nm;
    while (offset < block.size()) {
        require_bytes(offset, 3U, block.size(), "auxiliary field header");
        const char a = static_cast<char>(block[offset]);
        const char b = static_cast<char>(block[offset + 1U]);
        const char type = static_cast<char>(block[offset + 2U]);
        offset += 3U;
        std::optional<std::uint64_t> numeric;
        switch (type) {
            case 'A':
                require_bytes(offset, 1U, block.size(), "A tag");
                offset += 1U;
                break;
            case 'c': {
                require_bytes(offset, 1U, block.size(), "c tag");
                const auto value = static_cast<std::int8_t>(block[offset]);
                if (value >= 0) numeric = static_cast<std::uint64_t>(value);
                offset += 1U;
                break;
            }
            case 'C':
                require_bytes(offset, 1U, block.size(), "C tag");
                numeric = block[offset];
                offset += 1U;
                break;
            case 's': {
                require_bytes(offset, 2U, block.size(), "s tag");
                const auto value = le_i16(block.data() + offset);
                if (value >= 0) numeric = static_cast<std::uint64_t>(value);
                offset += 2U;
                break;
            }
            case 'S':
                require_bytes(offset, 2U, block.size(), "S tag");
                numeric = le_u16(block.data() + offset);
                offset += 2U;
                break;
            case 'i': {
                require_bytes(offset, 4U, block.size(), "i tag");
                const auto value = le_i32(block.data() + offset);
                if (value >= 0) numeric = static_cast<std::uint64_t>(value);
                offset += 4U;
                break;
            }
            case 'I':
                require_bytes(offset, 4U, block.size(), "I tag");
                numeric = le_u32(block.data() + offset);
                offset += 4U;
                break;
            case 'f':
                require_bytes(offset, 4U, block.size(), "f tag");
                offset += 4U;
                break;
            case 'Z':
            case 'H': {
                const auto begin = offset;
                while (offset < block.size() && block[offset] != 0U) ++offset;
                if (offset == block.size()) {
                    throw std::invalid_argument("Malformed BAM string auxiliary field");
                }
                if (type == 'H' && ((offset - begin) % 2U) != 0U) {
                    throw std::invalid_argument("Malformed BAM hex auxiliary field");
                }
                ++offset;
                break;
            }
            case 'B': {
                require_bytes(offset, 5U, block.size(), "B tag");
                const char subtype = static_cast<char>(block[offset]);
                ++offset;
                const std::uint32_t count = le_u32(block.data() + offset);
                offset += 4U;
                std::size_t width = 0U;
                switch (subtype) {
                    case 'c': case 'C': width = 1U; break;
                    case 's': case 'S': width = 2U; break;
                    case 'i': case 'I': case 'f': width = 4U; break;
                    default: throw std::invalid_argument("Malformed BAM B-tag subtype");
                }
                if (count > (std::numeric_limits<std::size_t>::max() / width)) {
                    throw std::invalid_argument("Malformed BAM B-tag count");
                }
                const auto bytes = static_cast<std::size_t>(count) * width;
                require_bytes(offset, bytes, block.size(), "B tag values");
                offset += bytes;
                break;
            }
            default:
                throw std::invalid_argument("Unsupported BAM auxiliary tag type");
        }
        if (a == 'N' && b == 'M') {
            if (nm.has_value()) {
                throw std::invalid_argument("BAM record contains duplicate NM tags");
            }
            if (!numeric.has_value()) {
                throw std::invalid_argument("BAM NM tag must be a non-negative integer");
            }
            nm = numeric;
        }
    }
    return nm;
}

[[nodiscard]] bool gzip_magic(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("Unable to open alignment input");
    unsigned char p[2]{};
    input.read(reinterpret_cast<char*>(p), 2);
    if (input.bad()) throw std::runtime_error("Unable to inspect alignment input");
    return input.gcount() == 2 && p[0] == 0x1FU && p[1] == 0x8BU;
}

}  // namespace

AlignmentQcStatistics analyze_sam(std::istream& input) {
    AlignmentQcStatistics stats;
    stats.input_format = "sam";
    std::map<std::string, std::uint64_t> reference_lengths;
    CoverageEvents coverage_events;
    std::string line;
    bool first = true;
    bool saw_record = false;
    while (read_bounded_line(input, line)) {
        if (first) {
            strip_utf8_bom(line);
            first = false;
        }
        if (line.empty()) throw std::invalid_argument("SAM contains an empty line");
        if (line.front() == '@' && !saw_record) {
            if (line.rfind("@SQ\t", 0U) == 0U) {
                const auto fields = split_tabs(line);
                std::optional<std::string> name;
                std::optional<std::uint64_t> length;
                for (std::size_t i = 1U; i < fields.size(); ++i) {
                    if (fields[i].rfind("SN:", 0U) == 0U) {
                        if (name.has_value()) throw std::invalid_argument("SAM @SQ contains duplicate SN tags");
                        name = std::string{fields[i].substr(3U)};
                    } else if (fields[i].rfind("LN:", 0U) == 0U) {
                        if (length.has_value()) throw std::invalid_argument("SAM @SQ contains duplicate LN tags");
                        length = parse_unsigned<std::uint64_t>(fields[i].substr(3U), "SAM @SQ LN");
                    }
                }
                if (!name.has_value() || name->empty() || !length.has_value() || *length == 0U ||
                    *length > maximum_reference_length || reference_lengths.contains(*name)) {
                    throw std::invalid_argument("SAM @SQ reference name/length is missing, invalid, or duplicated");
                }
                reference_lengths.emplace(*name, *length);
            }
            continue;
        }
        saw_record = true;
        if (line.front() == '@') {
            throw std::invalid_argument("SAM header line appears after alignment records");
        }
        const auto fields = split_tabs(line);
        if (fields.size() < 11U) {
            throw std::invalid_argument("SAM alignment record has fewer than 11 fields");
        }
        const auto flag = parse_unsigned<std::uint16_t>(fields[1], "SAM FLAG");
        const auto pos = parse_unsigned<std::uint64_t>(fields[3], "SAM POS");
        const auto mapq = parse_unsigned<std::uint32_t>(fields[4], "SAM MAPQ");
        const auto template_length = parse_signed_i64(fields[8], "SAM TLEN");
        if (mapq > 255U) throw std::invalid_argument("SAM MAPQ exceeds 255");
        const bool mapped = (flag & flag_unmapped) == 0U;
        if (mapped && (fields[2] == "*" || pos == 0U)) {
            throw std::invalid_argument("Mapped SAM record has invalid RNAME/POS");
        }
        if (mapped && !reference_lengths.empty() &&
            !reference_lengths.contains(std::string{fields[2]})) {
            throw std::invalid_argument("Mapped SAM record references an undeclared @SQ contig");
        }
        observe_record(stats, flag, mapq, fields[2], sam_nm(fields));
        if (mapped) {
            const auto operations = parse_sam_cigar(fields[5]);
            observe_coverage(
                coverage_events, reference_lengths, flag, fields[2], pos - 1U,
                operations, !reference_lengths.empty()
            );
        }
        const bool mate_mapped = (flag & flag_mate_unmapped) == 0U;
        const bool same_reference = mapped && mate_mapped &&
            (fields[6] == "=" || fields[6] == fields[2]);
        observe_template_length(stats, flag, same_reference, template_length);
    }
    if (!saw_record) throw std::invalid_argument("SAM contains no alignment records");
    finalize_coverage(stats, reference_lengths, coverage_events);
    return stats;
}

AlignmentQcStatistics analyze_bam(std::istream& input) {
    AlignmentQcStatistics stats;
    stats.input_format = "bam";
    unsigned char magic[4]{};
    read_exact(input, magic, sizeof(magic), "magic");
    if (!(magic[0] == 'B' && magic[1] == 'A' && magic[2] == 'M' && magic[3] == 1U)) {
        throw std::invalid_argument("BAM magic is invalid");
    }
    const std::int32_t l_text = read_le_i32(input, "header text length");
    if (l_text < 0 || static_cast<std::uint32_t>(l_text) > maximum_bam_header_text_bytes) {
        throw std::invalid_argument("BAM header text length exceeds safety bound");
    }
    std::vector<char> header(static_cast<std::size_t>(l_text));
    read_exact(input, header.data(), header.size(), "header text");
    const std::int32_t n_ref = read_le_i32(input, "reference count");
    if (n_ref < 0 || static_cast<std::uint32_t>(n_ref) > maximum_bam_reference_count) {
        throw std::invalid_argument("BAM reference count exceeds safety bound");
    }
    std::vector<std::string> references;
    references.reserve(static_cast<std::size_t>(n_ref));
    std::map<std::string, std::uint64_t> reference_lengths;
    for (std::int32_t i = 0; i < n_ref; ++i) {
        const std::int32_t l_name = read_le_i32(input, "reference name length");
        if (l_name <= 1 || static_cast<std::uint32_t>(l_name) > maximum_bam_reference_name_bytes) {
            throw std::invalid_argument("BAM reference name length is invalid");
        }
        std::vector<char> name(static_cast<std::size_t>(l_name));
        read_exact(input, name.data(), name.size(), "reference name");
        if (name.back() != '\0') {
            throw std::invalid_argument("BAM reference name is not NUL terminated");
        }
        name.pop_back();
        if (name.empty()) throw std::invalid_argument("BAM reference name is empty");
        const std::string reference_name{name.begin(), name.end()};
        const std::int32_t l_ref = read_le_i32(input, "reference length");
        if (l_ref <= 0 || static_cast<std::uint64_t>(l_ref) > maximum_reference_length) {
            throw std::invalid_argument("BAM reference length is invalid");
        }
        if (!reference_lengths.emplace(reference_name, static_cast<std::uint64_t>(l_ref)).second) {
            throw std::invalid_argument("BAM reference name is duplicated");
        }
        references.push_back(reference_name);
    }

    CoverageEvents coverage_events;
    bool saw_record = false;
    for (;;) {
        unsigned char size_bytes[4]{};
        input.read(reinterpret_cast<char*>(size_bytes), 4);
        const auto got = input.gcount();
        if (got == 0) {
            if (input.bad()) throw std::runtime_error("Unable to read BAM record size");
            break;
        }
        if (got != 4) throw std::invalid_argument("BAM ended within a record-size field");
        const auto block_size = le_i32(size_bytes);
        if (block_size < 32 || block_size > maximum_bam_record_bytes) {
            throw std::invalid_argument("BAM record block size is outside safety bounds");
        }
        std::vector<unsigned char> block(static_cast<std::size_t>(block_size));
        read_exact(input, block.data(), block.size(), "alignment record");
        saw_record = true;

        const std::int32_t ref_id = le_i32(block.data());
        const std::int32_t pos = le_i32(block.data() + 4U);
        const std::uint32_t bin_mq_nl = le_u32(block.data() + 8U);
        const std::uint32_t flag_nc = le_u32(block.data() + 12U);
        const std::int32_t l_seq = le_i32(block.data() + 16U);
        const std::int32_t next_ref_id = le_i32(block.data() + 20U);
        const std::int32_t next_pos = le_i32(block.data() + 24U);
        const std::int32_t template_length = le_i32(block.data() + 28U);
        const std::uint8_t l_read_name = static_cast<std::uint8_t>(bin_mq_nl & 0xFFU);
        const std::uint32_t mapq = (bin_mq_nl >> 8U) & 0xFFU;
        const std::uint16_t n_cigar = static_cast<std::uint16_t>(flag_nc & 0xFFFFU);
        const std::uint16_t flag = static_cast<std::uint16_t>((flag_nc >> 16U) & 0xFFFFU);
        if (l_seq < 0 || l_read_name < 2U) {
            throw std::invalid_argument("BAM alignment core fields are invalid");
        }
        std::size_t offset = 32U;
        require_bytes(offset, l_read_name, block.size(), "read name");
        if (block[offset + l_read_name - 1U] != 0U) {
            throw std::invalid_argument("BAM read name is not NUL terminated");
        }
        offset += l_read_name;
        const auto cigar_offset = offset;
        const auto cigar_bytes = static_cast<std::size_t>(n_cigar) * 4U;
        require_bytes(offset, cigar_bytes, block.size(), "CIGAR");
        offset += cigar_bytes;
        const auto seq_bytes = (static_cast<std::size_t>(l_seq) + 1U) / 2U;
        require_bytes(offset, seq_bytes, block.size(), "sequence");
        offset += seq_bytes;
        require_bytes(offset, static_cast<std::size_t>(l_seq), block.size(), "quality");
        offset += static_cast<std::size_t>(l_seq);

        const bool mapped = (flag & flag_unmapped) == 0U;
        std::string_view contig{"*"};
        if (mapped) {
            if (ref_id < 0 || ref_id >= n_ref || pos < 0) {
                throw std::invalid_argument("Mapped BAM record has invalid reference id/position");
            }
            contig = references[static_cast<std::size_t>(ref_id)];
        }
        const bool mate_mapped = (flag & flag_mate_unmapped) == 0U;
        if (mate_mapped && (flag & flag_paired) != 0U) {
            if (next_ref_id < 0 || next_ref_id >= n_ref || next_pos < 0) {
                throw std::invalid_argument("Mapped BAM mate has invalid reference id/position");
            }
        }
        observe_record(stats, flag, mapq, contig, bam_nm(block, offset));
        if (mapped) {
            const auto operations = parse_bam_cigar(block, cigar_offset, n_cigar);
            observe_coverage(
                coverage_events, reference_lengths, flag, contig,
                static_cast<std::uint64_t>(pos), operations, !reference_lengths.empty()
            );
        }
        observe_template_length(
            stats, flag, mapped && mate_mapped && ref_id == next_ref_id,
            static_cast<std::int64_t>(template_length)
        );
    }
    if (!saw_record) throw std::invalid_argument("BAM contains no alignment records");
    finalize_coverage(stats, reference_lengths, coverage_events);
    return stats;
}

AlignmentQcStatistics analyze_alignment_file(
    const std::filesystem::path& path,
    const std::string_view file_type
) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
        throw std::invalid_argument("Alignment QC input must be a regular non-symlink file");
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0U) throw std::invalid_argument("Alignment QC input must be non-empty");
    if (file_type == "sam") {
        if (gzip_magic(path)) {
            throw std::invalid_argument("SAM QC input must be plain text, not gzip/BGZF");
        }
        std::ifstream input{path, std::ios::binary};
        if (!input) throw std::runtime_error("Unable to open SAM input");
        return analyze_sam(input);
    }
    if (file_type == "bam") {
        if (!gzip_magic(path)) {
            throw std::invalid_argument("BAM QC input must be BGZF/gzip encoded");
        }
        GzipInputStream input{path};
        return analyze_bam(input);
    }
    throw std::invalid_argument("Alignment QC input file type must be sam or bam");
}

std::string render_alignment_qc_json(const AlignmentQcStatistics& s) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6);
    out << "{\n  \"format\": \"" << json_escape(s.input_format) << "\",\n"
        << "  \"totalRecords\": " << s.total_records << ",\n"
        << "  \"primaryRecords\": " << s.primary_records << ",\n"
        << "  \"primaryMapped\": " << s.primary_mapped << ",\n"
        << "  \"primaryUnmapped\": " << s.primary_unmapped << ",\n"
        << "  \"mappingRatePercent\": " << percentage(s.primary_mapped, s.primary_records) << ",\n"
        << "  \"secondaryRecords\": " << s.secondary_records << ",\n"
        << "  \"supplementaryRecords\": " << s.supplementary_records << ",\n"
        << "  \"duplicateRecords\": " << s.duplicate_records << ",\n"
        << "  \"qcFailedRecords\": " << s.qc_failed_records << ",\n"
        << "  \"pairedPrimaryRecords\": " << s.paired_primary_records << ",\n"
        << "  \"properPairPrimaryRecords\": " << s.proper_pair_primary_records << ",\n"
        << "  \"properPairRatePercent\": " << percentage(s.proper_pair_primary_records, s.paired_primary_records) << ",\n"
        << "  \"read1PrimaryRecords\": " << s.read1_primary_records << ",\n"
        << "  \"read2PrimaryRecords\": " << s.read2_primary_records << ",\n"
        << "  \"reversePrimaryMapped\": " << s.reverse_primary_mapped << ",\n"
        << "  \"mateUnmappedPrimaryRecords\": " << s.mate_unmapped_primary_records << ",\n"
        << "  \"averageMapq\": " << average(s.mapq_sum, s.mapq_observations) << ",\n"
        << "  \"minimumMapq\": " << (s.mapq_observations == 0U ? 0U : s.minimum_mapq) << ",\n"
        << "  \"maximumMapq\": " << s.maximum_mapq << ",\n"
        << "  \"mapqBins\": {\"0\": " << s.mapq_bins[0] << ", \"1-9\": " << s.mapq_bins[1]
        << ", \"10-19\": " << s.mapq_bins[2] << ", \"20-29\": " << s.mapq_bins[3]
        << ", \"30-39\": " << s.mapq_bins[4] << ", \"40-59\": " << s.mapq_bins[5]
        << ", \"60+\": " << s.mapq_bins[6] << "},\n"
        << "  \"nmObservedRecords\": " << s.nm_observations << ",\n"
        << "  \"nmMissingPrimaryMappedRecords\": " << (s.primary_mapped - s.nm_observations) << ",\n"
        << "  \"totalNmMismatches\": " << s.nm_sum << ",\n"
        << "  \"averageNm\": " << average(s.nm_sum, s.nm_observations) << ",\n"
        << "  \"minimumNm\": " << (s.nm_observations == 0U ? 0U : s.minimum_nm) << ",\n"
        << "  \"maximumNm\": " << s.maximum_nm << ",\n"
        << "  \"coverageAvailable\": " << (s.coverage_available ? "true" : "false") << ",\n"
        << "  \"totalReferenceBases\": " << s.total_reference_bases << ",\n"
        << "  \"coveredReferenceBases\": " << s.covered_reference_bases << ",\n"
        << "  \"coverageBreadthPercent\": " << percentage(s.covered_reference_bases, s.total_reference_bases) << ",\n"
        << "  \"meanDepth\": " << average(s.total_depth, s.total_reference_bases) << ",\n"
        << "  \"maximumDepth\": " << s.maximum_depth << ",\n"
        << "  \"basesAtLeast1x\": " << s.bases_at_least_1x << ",\n"
        << "  \"basesAtLeast10x\": " << s.bases_at_least_10x << ",\n"
        << "  \"basesAtLeast20x\": " << s.bases_at_least_20x << ",\n"
        << "  \"basesAtLeast30x\": " << s.bases_at_least_30x << ",\n"
        << "  \"templateLengthObservations\": " << s.template_length_observations << ",\n"
        << "  \"averageTemplateLength\": " << average(s.template_length_sum, s.template_length_observations) << ",\n"
        << "  \"minimumTemplateLength\": " << (s.template_length_observations == 0U ? 0U : s.minimum_template_length) << ",\n"
        << "  \"maximumTemplateLength\": " << s.maximum_template_length << ",\n"
        << "  \"contigs\": [";

    std::unordered_set<std::string> names;
    for (const auto& [name, count] : s.contig_primary_mapped) {
        static_cast<void>(count);
        names.insert(name);
    }
    for (const auto& [name, coverage] : s.contig_coverage) {
        static_cast<void>(coverage);
        names.insert(name);
    }
    std::vector<std::string> ordered_names{names.begin(), names.end()};
    std::sort(ordered_names.begin(), ordered_names.end());
    bool first = true;
    for (const auto& name : ordered_names) {
        if (!first) out << ',';
        first = false;
        const auto mapped = s.contig_primary_mapped.contains(name) ? s.contig_primary_mapped.at(name) : 0U;
        out << "{\"name\":\"" << json_escape(name) << "\",\"primaryMapped\":" << mapped;
        const auto found = s.contig_coverage.find(name);
        if (found != s.contig_coverage.end()) {
            const auto& coverage = found->second;
            out << ",\"referenceLength\":" << coverage.reference_length
                << ",\"coveredBases\":" << coverage.covered_bases
                << ",\"breadthPercent\":" << percentage(coverage.covered_bases, coverage.reference_length)
                << ",\"meanDepth\":" << average(coverage.total_depth, coverage.reference_length)
                << ",\"maximumDepth\":" << coverage.maximum_depth
                << ",\"basesAtLeast1x\":" << coverage.bases_at_least_1x
                << ",\"basesAtLeast10x\":" << coverage.bases_at_least_10x
                << ",\"basesAtLeast20x\":" << coverage.bases_at_least_20x
                << ",\"basesAtLeast30x\":" << coverage.bases_at_least_30x;
        }
        out << '}';
    }
    out << "]\n}\n";
    return out.str();
}

std::string render_alignment_qc_tsv(const AlignmentQcStatistics& s) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(6);
    out << "metric\tvalue\n"
        << "format\t" << s.input_format << "\n"
        << "total_records\t" << s.total_records << "\n"
        << "primary_records\t" << s.primary_records << "\n"
        << "primary_mapped\t" << s.primary_mapped << "\n"
        << "primary_unmapped\t" << s.primary_unmapped << "\n"
        << "mapping_rate_percent\t" << percentage(s.primary_mapped, s.primary_records) << "\n"
        << "secondary_records\t" << s.secondary_records << "\n"
        << "supplementary_records\t" << s.supplementary_records << "\n"
        << "duplicate_records\t" << s.duplicate_records << "\n"
        << "qc_failed_records\t" << s.qc_failed_records << "\n"
        << "paired_primary_records\t" << s.paired_primary_records << "\n"
        << "proper_pair_primary_records\t" << s.proper_pair_primary_records << "\n"
        << "proper_pair_rate_percent\t" << percentage(s.proper_pair_primary_records, s.paired_primary_records) << "\n"
        << "average_mapq\t" << average(s.mapq_sum, s.mapq_observations) << "\n"
        << "minimum_mapq\t" << (s.mapq_observations == 0U ? 0U : s.minimum_mapq) << "\n"
        << "maximum_mapq\t" << s.maximum_mapq << "\n"
        << "mapq_0\t" << s.mapq_bins[0] << "\n"
        << "mapq_1_9\t" << s.mapq_bins[1] << "\n"
        << "mapq_10_19\t" << s.mapq_bins[2] << "\n"
        << "mapq_20_29\t" << s.mapq_bins[3] << "\n"
        << "mapq_30_39\t" << s.mapq_bins[4] << "\n"
        << "mapq_40_59\t" << s.mapq_bins[5] << "\n"
        << "mapq_60_plus\t" << s.mapq_bins[6] << "\n"
        << "nm_observed_records\t" << s.nm_observations << "\n"
        << "nm_missing_primary_mapped_records\t" << (s.primary_mapped - s.nm_observations) << "\n"
        << "total_nm_mismatches\t" << s.nm_sum << "\n"
        << "average_nm\t" << average(s.nm_sum, s.nm_observations) << "\n"
        << "minimum_nm\t" << (s.nm_observations == 0U ? 0U : s.minimum_nm) << "\n"
        << "maximum_nm\t" << s.maximum_nm << "\n"
        << "coverage_available\t" << (s.coverage_available ? "true" : "false") << "\n"
        << "total_reference_bases\t" << s.total_reference_bases << "\n"
        << "covered_reference_bases\t" << s.covered_reference_bases << "\n"
        << "coverage_breadth_percent\t" << percentage(s.covered_reference_bases, s.total_reference_bases) << "\n"
        << "mean_depth\t" << average(s.total_depth, s.total_reference_bases) << "\n"
        << "maximum_depth\t" << s.maximum_depth << "\n"
        << "bases_at_least_1x\t" << s.bases_at_least_1x << "\n"
        << "bases_at_least_10x\t" << s.bases_at_least_10x << "\n"
        << "bases_at_least_20x\t" << s.bases_at_least_20x << "\n"
        << "bases_at_least_30x\t" << s.bases_at_least_30x << "\n"
        << "template_length_observations\t" << s.template_length_observations << "\n"
        << "average_template_length\t" << average(s.template_length_sum, s.template_length_observations) << "\n"
        << "minimum_template_length\t" << (s.template_length_observations == 0U ? 0U : s.minimum_template_length) << "\n"
        << "maximum_template_length\t" << s.maximum_template_length << "\n";
    for (const auto& [name, count] : s.contig_primary_mapped) {
        out << "contig_primary_mapped:" << name << '\t' << count << '\n';
    }
    for (const auto& [name, coverage] : s.contig_coverage) {
        out << "contig_reference_length:" << name << '\t' << coverage.reference_length << '\n'
            << "contig_covered_bases:" << name << '\t' << coverage.covered_bases << '\n'
            << "contig_breadth_percent:" << name << '\t' << percentage(coverage.covered_bases, coverage.reference_length) << '\n'
            << "contig_mean_depth:" << name << '\t' << average(coverage.total_depth, coverage.reference_length) << '\n'
            << "contig_maximum_depth:" << name << '\t' << coverage.maximum_depth << '\n';
    }
    return out.str();
}

}  // namespace biocore::alignment_qc
