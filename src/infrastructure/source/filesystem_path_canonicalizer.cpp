#include "biocore/infrastructure/filesystem_path_canonicalizer.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace biocore::infrastructure {
namespace {

[[nodiscard]] bool is_blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
               return std::isspace(static_cast<unsigned char>(character)) != 0;
           });
}

[[nodiscard]] std::filesystem::path path_from_utf8(const std::string_view value) {
#ifdef _WIN32
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char character : value) {
        encoded.push_back(static_cast<char8_t>(character));
    }
    return std::filesystem::path{encoded};
#else
    return std::filesystem::path{value};
#endif
}

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string encoded = path.generic_u8string();
    std::string result;
    result.reserve(encoded.size());
    for (const char8_t character : encoded) {
        result.push_back(static_cast<char>(character));
    }
    return result;
}

}  // namespace

std::string FilesystemPathCanonicalizer::canonicalize_existing_directory(const std::string_view path) {
    if (is_blank(path)) {
        throw std::invalid_argument("Project root path must not be blank");
    }
    if (path.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("Project root path must not contain NUL characters");
    }

    std::error_code error;
    const std::filesystem::path canonical_path = std::filesystem::canonical(path_from_utf8(path), error);
    if (error) {
        throw std::runtime_error("Unable to canonicalize the project root directory: " + error.message());
    }

    if (!std::filesystem::is_directory(canonical_path, error) || error) {
        throw std::invalid_argument("Project root path must identify an existing directory");
    }

    return path_to_utf8(canonical_path);
}

}  // namespace biocore::infrastructure
