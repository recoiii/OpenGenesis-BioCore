#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "biocore/application/i_plugin_registry.hpp"

namespace biocore::tests {

class FakePluginRegistry final : public application::IPluginRegistry {
public:
    FakePluginRegistry() {
        add_module("org.biocore.demo.validate");
        add_module("org.biocore.demo.scan");
        add_module("org.biocore.demo.report");
    }

    void add_module(
        std::string module_id,
        std::string executable_path = "/plugins/org.biocore.demo/bin/demo-plugin"
    ) {
        modules_.push_back(application::ResolvedPluginModule{
            .plugin_id = "org.biocore.demo",
            .plugin_version = "0.1.0",
            .module_id = std::move(module_id),
            .module_type = domain::PluginModuleType::process,
            .plugin_root_path = "/plugins/org.biocore.demo",
            .executable_path = std::move(executable_path),
            .parameters = {}, .inputs = {}, .outputs = {},
        });
    }

    [[nodiscard]] std::optional<application::ResolvedPluginModule> find_module(
        const std::string_view module_id
    ) const override {
        for (const auto& module : modules_) {
            if (module.module_id == module_id) return module;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::vector<application::RegisteredPlugin> list_plugins() const override {
        return {
            application::RegisteredPlugin{
                .id = "org.biocore.demo",
                .name = "Demo",
                .version = "0.1.0",
                .publisher = "BioCore",
                .root_path = "/plugins/org.biocore.demo",
            },
        };
    }

private:
    std::vector<application::ResolvedPluginModule> modules_;
};

}  // namespace biocore::tests
