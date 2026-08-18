#pragma once

#include <optional>
#include <string_view>

namespace biocore::domain {

enum class PluginPlatform {
    linux_x64,
    linux_arm64,
    windows_x64,
    windows_arm64,
};

[[nodiscard]] std::string_view to_string(PluginPlatform platform) noexcept;
[[nodiscard]] std::optional<PluginPlatform> plugin_platform_from_string(
    std::string_view value
) noexcept;

}  // namespace biocore::domain
