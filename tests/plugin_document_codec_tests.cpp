#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include "biocore/plugin_protocol/plugin_document_codec.hpp"

namespace {

using namespace biocore::plugin_protocol;

template <typename Function>
[[nodiscard]] bool rejects(Function&& function) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

[[nodiscard]] PluginManifestDocument document() {
    return PluginManifestDocument{
        .manifest_version = 1U,
        .id = "org.biocore.demo",
        .name = "Demo \"Plugin\"",
        .version = "0.1.0",
        .api_version = "1.0",
        .publisher = "BioCore Project",
        .modules = {
            PluginModuleDocument{
                .id = "org.biocore.demo.validate",
                .type = "process",
                .entrypoints = {
                    PluginEntrypointDocument{"linux-x64", "bin/linux-x64/demo"},
                    PluginEntrypointDocument{"windows-x64", "bin/windows-x64/demo.exe"},
                },
                .parameters = {}, .inputs = {}, .outputs = {},
            },
        },
    };
}

[[nodiscard]] bool round_trip_contract() {
    const auto source = document();
    const std::string encoded = serialize_plugin_manifest_document(source);
    const auto decoded = parse_plugin_manifest_document(encoded);
    return decoded.id == source.id && decoded.name == source.name &&
           decoded.modules.size() == 1U && decoded.modules.front().entrypoints.size() == 2U &&
           decoded.modules.front().entrypoints.front().platform == "linux-x64";
}

[[nodiscard]] bool version_two_contract() {
    PluginManifestDocument value = document();
    value.manifest_version = 2U;
    value.modules.front().parameters = {
        PluginParameterDocument{
            .name = "repeat", .type = "integer", .required = false,
            .default_value = std::string{"2"}, .minimum = std::string{"1"},
            .maximum = std::string{"3"}, .enum_values = {},
        },
    };
    value.modules.front().inputs = {
        PluginInputPortDocument{.name = "source", .required = true, .accepted_file_types = {"txt"}},
    };
    value.modules.front().outputs = {
        PluginOutputPortDocument{.name = "result", .file_type = "txt"},
    };
    const auto decoded = parse_plugin_manifest_document(serialize_plugin_manifest_document(value));
    const bool round_trip = decoded.manifest_version == 2U && decoded.modules.size() == 1U &&
        decoded.modules.front().parameters.size() == 1U &&
        decoded.modules.front().parameters.front().default_value == std::optional<std::string>{"2"} &&
        decoded.modules.front().inputs.size() == 1U &&
        decoded.modules.front().outputs.size() == 1U;

    PluginManifestDocument illegal_v1 = value;
    illegal_v1.manifest_version = 1U;
    const bool v1_rejected = rejects([&] {
        static_cast<void>(serialize_plugin_manifest_document(illegal_v1));
    });
    return round_trip && v1_rejected;
}

[[nodiscard]] bool invocation_contract() {
    PluginInvocationDocument source{
        .schema_version = 1U,
        .job_id = "job-1",
        .job_revision = 4,
        .step_id = "copy",
        .module_id = "org.biocore.demo.copy",
        .parameters = {
            PluginInvocationParameterDocument{"repeat", "integer", "2"},
            PluginInvocationParameterDocument{"enabled", "boolean", "true"},
        },
        .inputs = {
            PluginInvocationInputDocument{"source", "managed_file", "file-1", "txt", "/project/inputs/file-1/source.txt"},
        },
        .outputs = {
            PluginInvocationOutputDocument{"result", "txt", "/project/outputs/result.txt"},
        },
    };
    const auto decoded = parse_plugin_invocation_document(serialize_plugin_invocation_document(source));
    const bool round_trip = decoded.job_id == source.job_id && decoded.job_revision == 4 &&
        decoded.parameters.size() == 2U && decoded.inputs.size() == 1U && decoded.outputs.size() == 1U &&
        decoded.parameters.front().value == "2" && decoded.inputs.front().source_kind == "managed_file";
    const bool unknown_rejected = rejects([] {
        static_cast<void>(parse_plugin_invocation_document(
            R"({"schemaVersion":1,"jobId":"job-1","jobRevision":0,"stepId":"copy","moduleId":"org.biocore.demo.copy","parameters":[],"inputs":[],"outputs":[],"unknown":true})"
        ));
    });
    const bool duplicate_rejected = rejects([] {
        static_cast<void>(parse_plugin_invocation_document(
            R"({"schemaVersion":1,"jobId":"job-1","jobId":"job-2","jobRevision":0,"stepId":"copy","moduleId":"org.biocore.demo.copy","parameters":[],"inputs":[],"outputs":[]})"
        ));
    });
    return round_trip && unknown_rejected && duplicate_rejected;
}

[[nodiscard]] bool strict_contract() {
    return rejects([] {
               static_cast<void>(parse_plugin_manifest_document(
                   R"({"manifestVersion":1,"id":"org.biocore.demo","name":"P","version":"1.0.0","apiVersion":"1.0","publisher":"X","unknown":1,"modules":[{"id":"org.biocore.demo.m","type":"process","entrypoints":{"linux-x64":"bin/demo"}}]})"
               ));
           }) &&
           rejects([] {
               static_cast<void>(parse_plugin_manifest_document(
                   R"({"manifestVersion":1,"id":"p","id":"q","name":"P","version":"1.0.0","apiVersion":"1.0","publisher":"X","modules":[]})"
               ));
           }) &&
           rejects([] {
               static_cast<void>(parse_plugin_manifest_document(
                   R"({"manifestVersion":2,"id":"p","name":"P","version":"1.0.0","apiVersion":"1.0","publisher":"X","modules":[]})"
               ));
           }) &&
           rejects([] {
               static_cast<void>(parse_plugin_manifest_document(
                   R"({"manifestVersion":1,"id":"p","name":"P","version":"1.0.0","apiVersion":"1.0","publisher":"X","modules":[{"id":"p.m","type":"process","entrypoints":{"linux-x64":1}}]})"
               ));
           }) &&
           rejects([] {
               std::string invalid = R"({"manifestVersion":1,"id":")";
               invalid.push_back(static_cast<char>(0xFF));
               invalid += R"(","name":"P","version":"1.0.0","apiVersion":"1.0","publisher":"X","modules":[]})";
               static_cast<void>(parse_plugin_manifest_document(invalid));
           }) &&
           rejects([] {
               auto value = document();
               value.name = std::string{"bad\0name", 8U};
               static_cast<void>(serialize_plugin_manifest_document(value));
           });
}

}  // namespace

int main() {
    if (!round_trip_contract() || !version_two_contract() || !invocation_contract() || !strict_contract()) {
        std::cerr << "Plugin document codec tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Plugin document codec tests passed\n";
    return EXIT_SUCCESS;
}
