#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>

namespace biocore::infrastructure {

[[nodiscard]] std::string sha256_hex(std::span<const std::byte> bytes);
[[nodiscard]] std::string sha256_file_hex(const std::filesystem::path& path);

}  // namespace biocore::infrastructure
