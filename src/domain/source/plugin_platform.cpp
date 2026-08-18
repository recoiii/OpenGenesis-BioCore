#include "biocore/domain/plugin_platform.hpp"

namespace biocore::domain {

std::string_view to_string(const PluginPlatform platform) noexcept {
    switch (platform) {
        case PluginPlatform::linux_x64: return "linux-x64";
        case PluginPlatform::linux_arm64: return "linux-arm64";
        case PluginPlatform::windows_x64: return "windows-x64";
        case PluginPlatform::windows_arm64: return "windows-arm64";
    }
    return "unknown";
}

std::optional<PluginPlatform> plugin_platform_from_string(
    const std::string_view value
) noexcept {
    if (value == "linux-x64") return PluginPlatform::linux_x64;
    if (value == "linux-arm64") return PluginPlatform::linux_arm64;
    if (value == "windows-x64") return PluginPlatform::windows_x64;
    if (value == "windows-arm64") return PluginPlatform::windows_arm64;
    return std::nullopt;
}

}  // namespace biocore::domain
