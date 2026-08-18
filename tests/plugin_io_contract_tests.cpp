#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "biocore/domain/plugin_io_contract.hpp"
#include "biocore/domain/plugin_module_definition.hpp"

namespace {
using namespace biocore::domain;

template <typename Function>
[[nodiscard]] bool rejects(Function&& function) {
    try { function(); } catch (const std::invalid_argument&) { return true; }
    return false;
}

[[nodiscard]] bool parameter_contract() {
    PluginParameterDefinition repeat{
        "repeat", PluginParameterType::integer, false, PluginParameterValue{std::int64_t{2}}, 1.0, 3.0, {}
    };
    PluginParameterDefinition mode{
        "mode", PluginParameterType::enumeration, true, std::nullopt, std::nullopt, std::nullopt,
        {"copy", "touch"}
    };
    repeat.validate_value(PluginParameterValue{std::int64_t{3}});
    mode.validate_value(PluginParameterValue{std::string{"copy"}});
    return repeat.default_value().has_value() &&
           plugin_parameter_value_to_string(*repeat.default_value(), repeat.type()) == "2" &&
           rejects([&] { repeat.validate_value(PluginParameterValue{std::int64_t{4}}); }) &&
           rejects([&] { repeat.validate_value(PluginParameterValue{std::string{"2"}}); }) &&
           rejects([&] { mode.validate_value(PluginParameterValue{std::string{"other"}}); }) &&
           std::get<bool>(plugin_parameter_value_from_string("true", PluginParameterType::boolean));
}

[[nodiscard]] bool file_port_contract() {
    PluginInputPortDefinition input{"source", true, {"fastq", "txt"}};
    PluginOutputPortDefinition output{"result", "txt"};
    return input.required() && input.accepts_file_type("txt") && !input.accepts_file_type("vcf") &&
           output.file_type() == "txt" &&
           rejects([] { static_cast<void>(PluginInputPortDefinition{"Bad", true, {"txt"}}); }) &&
           rejects([] { static_cast<void>(PluginOutputPortDefinition{"result", ""}); });
}

[[nodiscard]] bool module_contract_uniqueness() {
    return rejects([] {
        static_cast<void>(PluginModuleDefinition{
            "org.biocore.demo.copy", PluginModuleType::process,
            {PluginEntrypoint{PluginPlatform::linux_x64, "bin/demo"}},
            {PluginParameterDefinition{"source", PluginParameterType::string, false}},
            {PluginInputPortDefinition{"source", true, {"txt"}}}, {}
        });
    });
}
}  // namespace

int main() {
    if (!parameter_contract() || !file_port_contract() || !module_contract_uniqueness()) {
        std::cerr << "Plugin I/O contract tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Plugin I/O contract tests passed\n";
    return EXIT_SUCCESS;
}
