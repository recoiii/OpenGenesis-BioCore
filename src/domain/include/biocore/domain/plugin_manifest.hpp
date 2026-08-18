#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/domain/plugin_module_definition.hpp"

namespace biocore::domain {

class PluginManifest final {
public:
    static constexpr std::uint32_t current_manifest_version = 2U;
    static constexpr std::uint32_t minimum_manifest_version = 1U;
    static constexpr std::string_view supported_api_version = "1.0";
    static constexpr std::size_t maximum_id_length = 128U;
    static constexpr std::size_t maximum_name_length = 256U;
    static constexpr std::size_t maximum_version_length = 64U;
    static constexpr std::size_t maximum_api_version_length = 32U;
    static constexpr std::size_t maximum_publisher_length = 256U;
    static constexpr std::size_t maximum_modules = 128U;

    PluginManifest(
        std::uint32_t manifest_version,
        std::string id,
        std::string name,
        std::string version,
        std::string api_version,
        std::string publisher,
        std::vector<PluginModuleDefinition> modules
    );

    [[nodiscard]] std::uint32_t manifest_version() const noexcept;
    [[nodiscard]] std::string_view id() const noexcept;
    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] std::string_view version() const noexcept;
    [[nodiscard]] std::string_view api_version() const noexcept;
    [[nodiscard]] std::string_view publisher() const noexcept;
    [[nodiscard]] const std::vector<PluginModuleDefinition>& modules() const noexcept;

private:
    std::uint32_t manifest_version_;
    std::string id_;
    std::string name_;
    std::string version_;
    std::string api_version_;
    std::string publisher_;
    std::vector<PluginModuleDefinition> modules_;
};

}  // namespace biocore::domain
