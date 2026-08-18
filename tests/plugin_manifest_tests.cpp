#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "biocore/domain/plugin_manifest.hpp"

namespace {

using biocore::domain::PluginEntrypoint;
using biocore::domain::PluginManifest;
using biocore::domain::PluginModuleDefinition;
using biocore::domain::PluginModuleType;
using biocore::domain::PluginPlatform;

template <typename Function>
[[nodiscard]] bool rejects(Function&& function) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

[[nodiscard]] PluginModuleDefinition module(
    std::string id = "org.biocore.demo.validate"
) {
    return PluginModuleDefinition{
        std::move(id),
        PluginModuleType::process,
        {
            PluginEntrypoint{PluginPlatform::linux_x64, "bin/linux-x64/demo"},
            PluginEntrypoint{PluginPlatform::windows_x64, "bin/windows-x64/demo.exe"},
        },
    };
}

[[nodiscard]] PluginManifest manifest() {
    return PluginManifest{
        1U,
        "org.biocore.demo",
        "Demo Plugin",
        "0.1.0",
        "1.0",
        "BioCore Project",
        {module()},
    };
}

[[nodiscard]] bool valid_contract() {
    const auto value = manifest();
    return value.id() == "org.biocore.demo" && value.version() == "0.1.0" &&
           value.modules().size() == 1U &&
           value.modules().front().entrypoint_for(PluginPlatform::linux_x64) ==
               std::optional<std::string_view>{"bin/linux-x64/demo"} &&
           biocore::domain::plugin_platform_from_string("windows-arm64") ==
               PluginPlatform::windows_arm64 &&
           biocore::domain::plugin_module_type_from_string("process") ==
               PluginModuleType::process;
}

[[nodiscard]] bool semantic_version_contract() {
    const auto prerelease_and_build = PluginManifest{
        1U,
        "org.biocore.demo",
        "Demo",
        "1.2.3-rc.1+build-7.sha",
        "1.0",
        "BioCore",
        {module()},
    };
    const auto build_with_hyphen = PluginManifest{
        1U,
        "org.biocore.demo",
        "Demo",
        "1.2.3+build-7",
        "1.0",
        "BioCore",
        {module()},
    };
    return prerelease_and_build.version() == "1.2.3-rc.1+build-7.sha" &&
           build_with_hyphen.version() == "1.2.3+build-7" &&
           rejects([] {
               static_cast<void>(PluginManifest{
                   1U, "org.biocore.demo", "Demo", "1.2.3-01", "1.0", "BioCore", {module()}
               });
           }) &&
           rejects([] {
               static_cast<void>(PluginManifest{
                   1U, "org.biocore.demo", "Demo", "1.2.3+", "1.0", "BioCore", {module()}
               });
           }) &&
           rejects([] {
               static_cast<void>(PluginManifest{
                   1U, "org.biocore.demo", "Demo", "1.2.3+build+other", "1.0", "BioCore", {module()}
               });
           });
}

[[nodiscard]] bool manifest_rejection_contract() {
    return rejects([] {
               static_cast<void>(PluginManifest{
                   3U, "org.biocore.demo", "Demo", "0.1.0", "1.0", "BioCore", {module()}
               });
           }) &&
           rejects([] {
               static_cast<void>(PluginManifest{
                   1U, "Org.BioCore", "Demo", "0.1.0", "1.0", "BioCore", {module()}
               });
           }) &&
           rejects([] {
               static_cast<void>(PluginManifest{
                   1U, "org.biocore.demo", "Demo", "01.0.0", "1.0", "BioCore", {module()}
               });
           }) &&
           rejects([] {
               static_cast<void>(PluginManifest{
                   1U, "org.biocore.demo", "Demo", "0.1.0", "2.0", "BioCore", {module()}
               });
           }) &&
           rejects([] {
               static_cast<void>(PluginManifest{
                   1U,
                   "org.biocore.demo",
                   "Demo",
                   "0.1.0",
                   "1.0",
                   "BioCore",
                   {module("org.other.module")}
               });
           });
}

[[nodiscard]] bool module_rejection_contract() {
    return rejects([] {
               static_cast<void>(PluginModuleDefinition{
                   "org.biocore.demo.module",
                   PluginModuleType::process,
                   {},
               });
           }) &&
           rejects([] {
               static_cast<void>(PluginModuleDefinition{
                   "org.biocore.demo.module",
                   PluginModuleType::process,
                   {PluginEntrypoint{PluginPlatform::linux_x64, "../escape"}},
               });
           }) &&
           rejects([] {
               static_cast<void>(PluginModuleDefinition{
                   "org.biocore.demo.module",
                   PluginModuleType::process,
                   {PluginEntrypoint{PluginPlatform::linux_x64, "bin\\demo"}},
               });
           }) &&
           rejects([] {
               static_cast<void>(PluginModuleDefinition{
                   "org.biocore.demo.module",
                   PluginModuleType::process,
                   {
                       PluginEntrypoint{PluginPlatform::linux_x64, "bin/demo"},
                       PluginEntrypoint{PluginPlatform::linux_x64, "bin/other"},
                   },
               });
           });
}

}  // namespace

int main() {
    if (!valid_contract() || !semantic_version_contract() ||
        !manifest_rejection_contract() || !module_rejection_contract()) {
        std::cerr << "Plugin manifest tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Plugin manifest tests passed\n";
    return EXIT_SUCCESS;
}
