#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/domain/plugin_module_definition.hpp"

namespace biocore::application {

struct RegisteredPlugin final {
    std::string id;
    std::string name;
    std::string version;
    std::uint32_t manifest_version{2U};
    std::string api_version{"1.0"};
    std::string publisher;
    std::string root_path;
};

struct ResolvedPluginModule final {
    std::string plugin_id;
    std::string plugin_version;
    std::uint32_t plugin_manifest_version{2U};
    std::string plugin_api_version{"1.0"};
    std::string module_id;
    domain::PluginModuleType module_type{domain::PluginModuleType::process};
    std::string plugin_root_path;
    std::string executable_path;
    std::vector<domain::PluginParameterDefinition> parameters{};
    std::vector<domain::PluginInputPortDefinition> inputs{};
    std::vector<domain::PluginOutputPortDefinition> outputs{};
};

class IPluginRegistry {
public:
    virtual ~IPluginRegistry() = default;

    [[nodiscard]] virtual std::optional<ResolvedPluginModule> find_module(
        std::string_view module_id
    ) const = 0;
    [[nodiscard]] virtual std::vector<RegisteredPlugin> list_plugins() const = 0;
};

}  // namespace biocore::application
