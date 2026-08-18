#pragma once

#include <array>
#include <filesystem>
#include <fstream>
#include <istream>
#include <memory>
#include <streambuf>

#include <zlib.h>

namespace biocore::fastq_qc {

[[nodiscard]] bool input_has_gzip_magic(const std::filesystem::path& path);
[[nodiscard]] std::unique_ptr<std::istream> open_fastq_input(const std::filesystem::path& path);

class GzipStreamBuffer final : public std::streambuf {
public:
    explicit GzipStreamBuffer(const std::filesystem::path& path);
    ~GzipStreamBuffer() override;

    GzipStreamBuffer(const GzipStreamBuffer&) = delete;
    GzipStreamBuffer& operator=(const GzipStreamBuffer&) = delete;

protected:
    [[nodiscard]] int_type underflow() override;

private:
    void fill_compressed_input();
    void reset_for_next_member();
    [[noreturn]] void throw_inflate_error(const char* fallback) const;

    std::ifstream input_;
    z_stream stream_{};
    std::array<unsigned char, 64U * 1024U> compressed_buffer_{};
    std::array<char, 64U * 1024U> decompressed_buffer_{};
    bool initialized_{false};
    bool input_eof_{false};
    bool member_finished_{false};
};

class GzipInputStream final : public std::istream {
public:
    explicit GzipInputStream(const std::filesystem::path& path);

private:
    GzipStreamBuffer buffer_;
};

}  // namespace biocore::fastq_qc
