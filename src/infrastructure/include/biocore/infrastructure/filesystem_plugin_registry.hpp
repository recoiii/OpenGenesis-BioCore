#pragma once

#include <filesystem>
#include <shared_mutex>
#include <string>
#include <vector>

#include "biocore/application/i_plugin_registry.hpp"
#include "biocore/domain/plugin_platform.hpp"

namespace biocore::infrastructure {

struct PluginDiscoveryIssue final {
    std::string candidate_path;
    std::string message;
};

struct PluginDiscoveryReport final {
    std::size_t loaded_plugins{0U};
    std::size_t loaded_modules{0U};
    std::vector<PluginDiscoveryIssue> rejected;
};

[[nodiscard]] domain::PluginPlatform current_plugin_platform();

class FilesystemPluginRegistry final : public application::IPluginRegistry {
public:
    explicit FilesystemPluginRegistry(
        std::vector<std::filesystem::path> plugin_roots,
        domain::PluginPlatform platform = current_plugin_platform()
    );

    [[nodiscard]] PluginDiscoveryReport refresh();

    [[nodiscard]] std::optional<application::ResolvedPluginModule> find_module(
        std::string_view module_id
    ) const override;
    [[nodiscard]] std::vector<application::RegisteredPlugin> list_plugins() const override;

private:
    std::vector<std::filesystem::path> plugin_roots_;
    domain::PluginPlatform platform_;
    mutable std::shared_mutex mutex_;
    std::vector<application::RegisteredPlugin> plugins_;
    std::vector<application::ResolvedPluginModule> modules_;
};

}  // namespace biocore::infrastructure
