#include "fastq_input_stream.hpp"

#include <limits>
#include <stdexcept>
#include <string>

namespace biocore::fastq_qc {

bool input_has_gzip_magic(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        throw std::runtime_error("Unable to open FASTQ input");
    }

    unsigned char prefix[2]{};
    input.read(reinterpret_cast<char*>(prefix), 2);
    const auto count = input.gcount();
    if (input.bad()) {
        throw std::runtime_error("Unable to inspect FASTQ input");
    }
    return count == 2 && prefix[0] == 0x1FU && prefix[1] == 0x8BU;
}

std::unique_ptr<std::istream> open_fastq_input(const std::filesystem::path& path) {
    if (input_has_gzip_magic(path)) {
        return std::make_unique<GzipInputStream>(path);
    }
    auto input = std::make_unique<std::ifstream>(path, std::ios::binary);
    if (!*input) {
        throw std::runtime_error("Unable to open FASTQ input");
    }
    return input;
}

GzipStreamBuffer::GzipStreamBuffer(const std::filesystem::path& path)
    : input_{path, std::ios::binary} {
    if (!input_) {
        throw std::runtime_error("Unable to open gzip-compressed FASTQ input");
    }
    if (compressed_buffer_.size() > std::numeric_limits<uInt>::max() ||
        decompressed_buffer_.size() > std::numeric_limits<uInt>::max()) {
        throw std::logic_error("FASTQ gzip buffers exceed zlib limits");
    }

    stream_.zalloc = Z_NULL;
    stream_.zfree = Z_NULL;
    stream_.opaque = Z_NULL;
    const int code = inflateInit2(&stream_, 16 + MAX_WBITS);
    if (code != Z_OK) {
        throw std::runtime_error("Unable to initialize gzip FASTQ decompressor");
    }
    initialized_ = true;
    setg(
        decompressed_buffer_.data(),
        decompressed_buffer_.data(),
        decompressed_buffer_.data()
    );
}

GzipStreamBuffer::~GzipStreamBuffer() {
    if (initialized_) {
        (void)inflateEnd(&stream_);
    }
}

void GzipStreamBuffer::fill_compressed_input() {
    if (stream_.avail_in != 0U || input_eof_) {
        return;
    }

    input_.read(
        reinterpret_cast<char*>(compressed_buffer_.data()),
        static_cast<std::streamsize>(compressed_buffer_.size())
    );
    const auto count = input_.gcount();
    if (input_.bad()) {
        throw std::runtime_error("Unable to read gzip-compressed FASTQ input");
    }
    if (count == 0) {
        input_eof_ = true;
        return;
    }

    stream_.next_in = compressed_buffer_.data();
    stream_.avail_in = static_cast<uInt>(count);
}

void GzipStreamBuffer::reset_for_next_member() {
    Bytef* const remaining = stream_.next_in;
    const uInt remaining_size = stream_.avail_in;
    const int code = inflateReset2(&stream_, 16 + MAX_WBITS);
    if (code != Z_OK) {
        throw std::runtime_error("Unable to reset gzip FASTQ decompressor");
    }
    stream_.next_in = remaining;
    stream_.avail_in = remaining_size;
    member_finished_ = false;
}

[[noreturn]] void GzipStreamBuffer::throw_inflate_error(const char* fallback) const {
    std::string message{fallback};
    if (stream_.msg != nullptr && *stream_.msg != '\0') {
        message += ": ";
        message += stream_.msg;
    }
    throw std::runtime_error(message);
}

GzipStreamBuffer::int_type GzipStreamBuffer::underflow() {
    if (gptr() < egptr()) {
        return traits_type::to_int_type(*gptr());
    }

    stream_.next_out = reinterpret_cast<Bytef*>(decompressed_buffer_.data());
    stream_.avail_out = static_cast<uInt>(decompressed_buffer_.size());

    for (;;) {
        if (member_finished_) {
            fill_compressed_input();
            if (stream_.avail_in == 0U && input_eof_) {
                return traits_type::eof();
            }
            reset_for_next_member();
        }

        fill_compressed_input();
        if (stream_.avail_in == 0U && input_eof_) {
            throw std::runtime_error(
                "gzip FASTQ ended before a complete CRC/size trailer"
            );
        }

        const uInt output_before = stream_.avail_out;
        const uInt input_before = stream_.avail_in;
        const int code = inflate(&stream_, Z_NO_FLUSH);

        const auto produced = static_cast<std::size_t>(
            output_before - stream_.avail_out
        );
        if (code == Z_STREAM_END) {
            member_finished_ = true;
        } else if (code != Z_OK && code != Z_BUF_ERROR) {
            throw_inflate_error("Unable to decompress gzip FASTQ input");
        }

        if (produced != 0U) {
            setg(
                decompressed_buffer_.data(),
                decompressed_buffer_.data(),
                decompressed_buffer_.data() + produced
            );
            return traits_type::to_int_type(*gptr());
        }

        if (code == Z_BUF_ERROR &&
            input_before == stream_.avail_in &&
            output_before == stream_.avail_out &&
            stream_.avail_in != 0U) {
            throw std::runtime_error("gzip FASTQ decompressor made no progress");
        }
    }
}

GzipInputStream::GzipInputStream(const std::filesystem::path& path)
    : std::istream{nullptr}, buffer_{path} {
    rdbuf(&buffer_);
}

}  // namespace biocore::fastq_qc
