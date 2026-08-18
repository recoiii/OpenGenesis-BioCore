#include "alignment_io.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <streambuf>
#include <unordered_set>

#include <zlib.h>

namespace biocore::alignment {
namespace {

constexpr std::size_t maximum_line_bytes = 64U * 1024U * 1024U;
constexpr std::uintmax_t maximum_reference_file_bytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t maximum_reference_bases = 500000000ULL;
constexpr std::size_t maximum_contig_name_bytes = 255U;

[[nodiscard]] char uppercase_ascii(const char value) noexcept {
    return value >= 'a' && value <= 'z'
        ? static_cast<char>(value - ('a' - 'A')) : value;
}

[[nodiscard]] bool valid_iupac(const char raw) noexcept {
    switch (uppercase_ascii(raw)) {
        case 'A': case 'C': case 'G': case 'T': case 'N':
        case 'R': case 'Y': case 'S': case 'W': case 'K':
        case 'M': case 'B': case 'D': case 'H': case 'V':
            return true;
        default:
            return false;
    }
}

[[nodiscard]] bool read_bounded_line(std::istream& input, std::string& line) {
    line.clear();
    bool saw_any = false;
    for (;;) {
        const int next = input.get();
        if (next == std::char_traits<char>::eof()) {
            if (input.bad()) throw std::runtime_error("Unable to read sequence input");
            return saw_any;
        }
        saw_any = true;
        const char value = static_cast<char>(next);
        if (value == '\n') break;
        if (line.size() >= maximum_line_bytes) {
            throw std::invalid_argument("Sequence line exceeds the 64 MiB safety limit");
        }
        line.push_back(value);
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
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

[[nodiscard]] std::string first_token(const std::string_view value) {
    const auto begin = value.find_first_not_of(" \t");
    if (begin == std::string_view::npos) return {};
    const auto end = value.find_first_of(" \t", begin);
    return std::string{value.substr(begin, end - begin)};
}

class GzipStreamBuffer final : public std::streambuf {
public:
    explicit GzipStreamBuffer(const std::filesystem::path& path)
        : input_{path, std::ios::binary} {
        if (!input_) throw std::runtime_error("Unable to open gzip FASTQ input");
        stream_.zalloc = Z_NULL;
        stream_.zfree = Z_NULL;
        stream_.opaque = Z_NULL;
        if (inflateInit2(&stream_, 16 + MAX_WBITS) != Z_OK) {
            throw std::runtime_error("Unable to initialize gzip FASTQ decompressor");
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

        stream_.next_out = reinterpret_cast<Bytef*>(output_.data());
        stream_.avail_out = static_cast<uInt>(output_.size());

        for (;;) {
            if (member_finished_) {
                fill_input();
                if (stream_.avail_in == 0U && input_eof_) return traits_type::eof();
                Bytef* remaining = stream_.next_in;
                const uInt remaining_size = stream_.avail_in;
                if (inflateReset2(&stream_, 16 + MAX_WBITS) != Z_OK) {
                    throw std::runtime_error("Unable to reset gzip FASTQ decompressor");
                }
                stream_.next_in = remaining;
                stream_.avail_in = remaining_size;
                member_finished_ = false;
            }

            fill_input();
            if (stream_.avail_in == 0U && input_eof_) {
                throw std::runtime_error("gzip FASTQ ended before a complete CRC/size trailer");
            }

            const uInt output_before = stream_.avail_out;
            const uInt input_before = stream_.avail_in;
            const int code = inflate(&stream_, Z_NO_FLUSH);
            const auto produced = static_cast<std::size_t>(output_before - stream_.avail_out);
            if (code == Z_STREAM_END) member_finished_ = true;
            else if (code != Z_OK && code != Z_BUF_ERROR) {
                std::string message{"Unable to decompress gzip FASTQ input"};
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
                throw std::runtime_error("gzip FASTQ decompressor made no progress");
            }
        }
    }

private:
    void fill_input() {
        if (stream_.avail_in != 0U || input_eof_) return;
        input_.read(reinterpret_cast<char*>(compressed_.data()),
                    static_cast<std::streamsize>(compressed_.size()));
        const auto count = input_.gcount();
        if (input_.bad()) throw std::runtime_error("Unable to read gzip FASTQ input");
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

[[nodiscard]] bool gzip_magic(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("Unable to open FASTQ input");
    unsigned char prefix[2]{};
    input.read(reinterpret_cast<char*>(prefix), 2);
    if (input.bad()) throw std::runtime_error("Unable to inspect FASTQ input");
    return input.gcount() == 2 && prefix[0] == 0x1FU && prefix[1] == 0x8BU;
}

[[nodiscard]] char complement_iupac(const char raw) {
    switch (uppercase_ascii(raw)) {
        case 'A': return 'T'; case 'C': return 'G'; case 'G': return 'C'; case 'T': return 'A';
        case 'N': return 'N'; case 'R': return 'Y'; case 'Y': return 'R'; case 'S': return 'S';
        case 'W': return 'W'; case 'K': return 'M'; case 'M': return 'K'; case 'B': return 'V';
        case 'D': return 'H'; case 'H': return 'D'; case 'V': return 'B';
        default: throw std::invalid_argument("DNA sequence contains a non-IUPAC symbol");
    }
}

}  // namespace

FastqRecordReader::FastqRecordReader(std::istream& input) noexcept : input_{input} {}

bool FastqRecordReader::read(FastqRecord& record) {
    std::string separator;
    if (!read_bounded_line(input_, record.header)) return false;
    if (first_record_) {
        strip_initial_utf8_bom(record.header);
        first_record_ = false;
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
    if (separator.empty() || separator.front() != '+') {
        throw std::invalid_argument("FASTQ separator line must begin with '+'");
    }
    validate_fastq_record(record);
    return true;
}

std::unique_ptr<std::istream> open_fastq_input(const std::filesystem::path& path) {
    if (gzip_magic(path)) return std::make_unique<GzipInputStream>(path);
    auto input = std::make_unique<std::ifstream>(path, std::ios::binary);
    if (!*input) throw std::runtime_error("Unable to open FASTQ input");
    return input;
}

std::vector<ReferenceContig> read_reference_fasta(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status)) {
        throw std::invalid_argument("Reference FASTA must be a regular non-symlink file");
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0U || size > maximum_reference_file_bytes) {
        throw std::invalid_argument("Reference FASTA size is outside the Iteration 034 safety bound");
    }
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error("Unable to open reference FASTA");
    return parse_reference_fasta(input);
}

std::vector<ReferenceContig> parse_reference_fasta(std::istream& input) {
    std::vector<ReferenceContig> contigs;
    std::unordered_set<std::string> names;
    std::string line;
    std::uint64_t total_bases = 0U;
    bool first_line = true;

    while (read_bounded_line(input, line)) {
        if (first_line) {
            strip_initial_utf8_bom(line);
            first_line = false;
        }
        if (line.empty()) continue;
        if (line.front() == '>') {
            const auto name = first_token(std::string_view{line}.substr(1U));
            if (name.empty() || name.size() > maximum_contig_name_bytes ||
                name.find_first_of("\\\r\n") != std::string::npos) {
                throw std::invalid_argument("Reference FASTA contig identifier is invalid");
            }
            if (!names.insert(name).second) {
                throw std::invalid_argument("Reference FASTA contains duplicate contig identifiers");
            }
            contigs.push_back(ReferenceContig{name, {}});
            continue;
        }
        if (contigs.empty()) {
            throw std::invalid_argument("Reference FASTA sequence appears before the first header");
        }
        for (char& base : line) {
            if (!valid_iupac(base)) {
                throw std::invalid_argument("Reference FASTA contains a non-IUPAC DNA symbol");
            }
            base = uppercase_ascii(base);
        }
        if (line.size() > maximum_reference_bases - total_bases) {
            throw std::invalid_argument("Reference FASTA exceeds the Iteration 034 base-count safety bound");
        }
        total_bases += static_cast<std::uint64_t>(line.size());
        contigs.back().sequence += line;
    }

    if (contigs.empty() || total_bases == 0U) {
        throw std::invalid_argument("Reference FASTA contains no sequence");
    }
    for (const auto& contig : contigs) {
        if (contig.sequence.empty()) {
            throw std::invalid_argument("Reference FASTA contains an empty contig");
        }
    }
    return contigs;
}

void validate_fastq_record(const FastqRecord& record) {
    if (record.header.size() < 2U || record.header.front() != '@' ||
        first_token(std::string_view{record.header}.substr(1U)).empty()) {
        throw std::invalid_argument("FASTQ header is invalid");
    }
    if (record.sequence.empty()) throw std::invalid_argument("FASTQ contains an empty sequence");
    if (record.sequence.size() != record.quality.size()) {
        throw std::invalid_argument("FASTQ sequence and quality lengths do not match");
    }
    for (const char base : record.sequence) {
        if (!valid_iupac(base)) throw std::invalid_argument("FASTQ contains a non-IUPAC DNA symbol");
    }
    for (const char quality : record.quality) {
        const auto encoded = static_cast<unsigned char>(quality);
        if (encoded < 33U || encoded > 126U) {
            throw std::invalid_argument("FASTQ quality is outside the Phred+33 printable range");
        }
    }
}

MateIdentity parse_mate_identity(const std::string& header) {
    if (header.size() < 2U || header.front() != '@') {
        throw std::invalid_argument("Paired FASTQ header is invalid");
    }
    const std::string_view body{header.data() + 1U, header.size() - 1U};
    std::string primary = first_token(body);
    std::optional<unsigned int> mate;
    if (primary.size() > 2U && primary[primary.size() - 2U] == '/' &&
        (primary.back() == '1' || primary.back() == '2')) {
        mate = static_cast<unsigned int>(primary.back() - '0');
        primary.resize(primary.size() - 2U);
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
    if (primary.empty()) throw std::invalid_argument("Paired FASTQ header identifier is empty");
    return MateIdentity{std::move(primary), mate};
}

void validate_paired_fastq_records(const FastqRecord& read1, const FastqRecord& read2) {
    const auto identity1 = parse_mate_identity(read1.header);
    const auto identity2 = parse_mate_identity(read2.header);
    if (identity1.core_id != identity2.core_id) {
        throw std::invalid_argument("Paired FASTQ mate identifiers do not match");
    }
    if (identity1.mate.has_value() && *identity1.mate != 1U) {
        throw std::invalid_argument("Paired FASTQ read1 is marked as mate 2");
    }
    if (identity2.mate.has_value() && *identity2.mate != 2U) {
        throw std::invalid_argument("Paired FASTQ read2 is marked as mate 1");
    }
}

std::string sam_read_name(const std::string& header) {
    if (header.empty() || header.front() != '@') throw std::invalid_argument("FASTQ header is invalid");
    auto name = first_token(std::string_view{header}.substr(1U));
    if (name.empty()) throw std::invalid_argument("FASTQ header identifier is empty");
    return name;
}

std::string reverse_complement(const std::string_view sequence) {
    std::string result;
    result.reserve(sequence.size());
    for (auto it = sequence.rbegin(); it != sequence.rend(); ++it) {
        result.push_back(complement_iupac(*it));
    }
    return result;
}

std::string reverse_quality(const std::string_view quality) {
    return std::string{quality.rbegin(), quality.rend()};
}

}  // namespace biocore::alignment
