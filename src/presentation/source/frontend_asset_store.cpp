#include "biocore/presentation/frontend_asset_store.hpp"

#include <cstdint>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace biocore::presentation {
namespace {

[[nodiscard]] bool valid_utf8(const std::string_view value) noexcept {
    std::size_t index = 0U;
    while (index < value.size()) {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7fU) {
            ++index;
            continue;
        }

        std::size_t length = 0U;
        std::uint32_t code_point = 0U;
        if ((first & 0xe0U) == 0xc0U) {
            length = 2U;
            code_point = first & 0x1fU;
        } else if ((first & 0xf0U) == 0xe0U) {
            length = 3U;
            code_point = first & 0x0fU;
        } else if ((first & 0xf8U) == 0xf0U) {
            length = 4U;
            code_point = first & 0x07U;
        } else {
            return false;
        }
        if (index + length > value.size()) return false;

        for (std::size_t offset = 1U; offset < length; ++offset) {
            const auto continuation = static_cast<unsigned char>(value[index + offset]);
            if ((continuation & 0xc0U) != 0x80U) return false;
            code_point = (code_point << 6U) | (continuation & 0x3fU);
        }
        if ((length == 2U && code_point < 0x80U) ||
            (length == 3U && code_point < 0x800U) ||
            (length == 4U && code_point < 0x10000U) ||
            code_point > 0x10ffffU ||
            (code_point >= 0xd800U && code_point <= 0xdfffU)) {
            return false;
        }
        index += length;
    }
    return true;
}

[[nodiscard]] bool path_is_within(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate
) {
    auto root_iterator = root.begin();
    auto candidate_iterator = candidate.begin();
    for (; root_iterator != root.end(); ++root_iterator, ++candidate_iterator) {
        if (candidate_iterator == candidate.end() || *root_iterator != *candidate_iterator) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::filesystem::path require_root(std::filesystem::path root) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(root, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_directory(status)) {
        throw std::invalid_argument(
            "Frontend root must be an existing non-symlink directory"
        );
    }

    root = std::filesystem::canonical(root, error);
    if (error || root == root.root_path()) {
        throw std::invalid_argument("Frontend root could not be canonicalized safely");
    }
    return root;
}

[[nodiscard]] std::string read_asset(
    const std::filesystem::path& root,
    const std::filesystem::path& relative_path,
    const std::size_t maximum_bytes
) {
    const std::filesystem::path requested = root / relative_path;
    std::error_code error;
    const auto status = std::filesystem::symlink_status(requested, error);
    if (error || std::filesystem::is_symlink(status) ||
        !std::filesystem::is_regular_file(status)) {
        throw std::invalid_argument("Required frontend asset is missing or unsafe");
    }

    const auto canonical = std::filesystem::canonical(requested, error);
    if (error || !path_is_within(root, canonical) ||
        canonical != requested.lexically_normal()) {
        throw std::invalid_argument("Frontend asset must stay inside the canonical frontend root");
    }

    const auto size = std::filesystem::file_size(canonical, error);
    if (error || size == 0U || size > maximum_bytes) {
        throw std::invalid_argument("Frontend asset size is invalid");
    }

    std::ifstream input{canonical, std::ios::binary};
    if (!input) {
        throw std::runtime_error("Unable to open frontend asset");
    }
    std::string body{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}
    };
    if (input.bad() || body.size() != size) {
        throw std::runtime_error("Unable to read frontend asset completely");
    }
    if (body.find('\0') != std::string::npos) {
        throw std::invalid_argument("Frontend text assets must not contain NUL data");
    }
    if (!valid_utf8(body)) {
        throw std::invalid_argument("Frontend text assets must be valid UTF-8");
    }
    return body;
}

}  // namespace

FrontendAssetStore::FrontendAssetStore(std::filesystem::path root)
    : root_{require_root(std::move(root))},
      index_{
          .content_type = "text/html; charset=utf-8",
          .body = read_asset(root_, "index.html", maximum_html_bytes),
          .cache_control = "no-store",
      },
      css_{
          .content_type = "text/css; charset=utf-8",
          .body = read_asset(root_, "assets/app.css", maximum_css_bytes),
          .cache_control = "no-cache",
      },
      javascript_{
          .content_type = "text/javascript; charset=utf-8",
          .body = read_asset(root_, "assets/app.js", maximum_javascript_bytes),
          .cache_control = "no-cache",
      } {}

std::optional<FrontendAsset> FrontendAssetStore::find(
    const std::string_view request_path
) const {
    const LoadedAsset* asset = nullptr;
    if (request_path == "/" || request_path == "/index.html") {
        asset = &index_;
    } else if (request_path == "/assets/app.css") {
        asset = &css_;
    } else if (request_path == "/assets/app.js") {
        asset = &javascript_;
    } else {
        return std::nullopt;
    }

    return FrontendAsset{
        .content_type = asset->content_type,
        .body = asset->body,
        .cache_control = asset->cache_control,
    };
}

const std::filesystem::path& FrontendAssetStore::root() const noexcept {
    return root_;
}

}  // namespace biocore::presentation
