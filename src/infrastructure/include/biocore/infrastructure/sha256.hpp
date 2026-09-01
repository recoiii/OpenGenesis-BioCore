#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace biocore::infrastructure {

inline constexpr std::size_t file_stream_buffer_bytes = 64U * 1024U;

struct FileCopySha256Result final {
    std::uint64_t bytes_copied{0U};
    std::string sha256;
};

[[nodiscard]] std::string sha256_hex(std::span<const std::byte> bytes);
[[nodiscard]] std::string sha256_file_hex(const std::filesystem::path& path);
[[nodiscard]] FileCopySha256Result copy_file_with_sha256(
    const std::filesystem::path& source,
    const std::filesystem::path& destination
);

}  // namespace biocore::infrastructure
