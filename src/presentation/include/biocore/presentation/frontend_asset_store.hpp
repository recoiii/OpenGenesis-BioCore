#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace biocore::presentation {

struct FrontendAsset final {
    std::string content_type;
    std::string body;
    std::string cache_control;
};

class FrontendAssetStore final {
public:
    static constexpr std::size_t maximum_html_bytes = 128U * 1024U;
    static constexpr std::size_t maximum_css_bytes = 256U * 1024U;
    static constexpr std::size_t maximum_javascript_bytes = 256U * 1024U;

    explicit FrontendAssetStore(std::filesystem::path root);

    [[nodiscard]] std::optional<FrontendAsset> find(std::string_view request_path) const;
    [[nodiscard]] const std::filesystem::path& root() const noexcept;

private:
    struct LoadedAsset final {
        std::string content_type;
        std::string body;
        std::string cache_control;
    };

    std::filesystem::path root_;
    LoadedAsset index_;
    LoadedAsset css_;
    LoadedAsset javascript_;
};

}  // namespace biocore::presentation
