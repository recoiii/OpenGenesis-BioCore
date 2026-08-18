#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/domain/plugin_io_contract.hpp"
#include "biocore/domain/plugin_platform.hpp"

namespace biocore::domain {

enum class PluginModuleType {
    process,
};

[[nodiscard]] std::string_view to_string(PluginModuleType type) noexcept;
[[nodiscard]] std::optional<PluginModuleType> plugin_module_type_from_string(
    std::string_view value
) noexcept;

struct PluginEntrypoint final {
    PluginPlatform platform;
    std::string relative_path;
};

class PluginModuleDefinition final {
public:
    static constexpr std::size_t maximum_id_length = 256U;
    static constexpr std::size_t maximum_entrypoints = 8U;
    static constexpr std::size_t maximum_relative_path_length = 1024U;

    PluginModuleDefinition(
        std::string id,
        PluginModuleType type,
        std::vector<PluginEntrypoint> entrypoints,
        std::vector<PluginParameterDefinition> parameters = {},
        std::vector<PluginInputPortDefinition> inputs = {},
        std::vector<PluginOutputPortDefinition> outputs = {}
    );

    [[nodiscard]] std::string_view id() const noexcept;
    [[nodiscard]] PluginModuleType type() const noexcept;
    [[nodiscard]] const std::vector<PluginEntrypoint>& entrypoints() const noexcept;
    [[nodiscard]] std::optional<std::string_view> entrypoint_for(
        PluginPlatform platform
    ) const noexcept;
    [[nodiscard]] const std::vector<PluginParameterDefinition>& parameters() const noexcept;
    [[nodiscard]] const std::vector<PluginInputPortDefinition>& inputs() const noexcept;
    [[nodiscard]] const std::vector<PluginOutputPortDefinition>& outputs() const noexcept;

private:
    std::string id_;
    PluginModuleType type_;
    std::vector<PluginEntrypoint> entrypoints_;
    std::vector<PluginParameterDefinition> parameters_;
    std::vector<PluginInputPortDefinition> inputs_;
    std::vector<PluginOutputPortDefinition> outputs_;
};

}  // namespace biocore::domain
