#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace biocore::plugin_protocol {

inline constexpr std::uint32_t current_plugin_manifest_version = 2U;
inline constexpr std::uint32_t minimum_plugin_manifest_version = 1U;
inline constexpr std::uint32_t current_plugin_invocation_schema_version = 1U;
inline constexpr std::size_t maximum_plugin_manifest_bytes = 256U * 1024U;
inline constexpr std::size_t maximum_plugin_invocation_bytes = 256U * 1024U;

struct PluginEntrypointDocument final {
    std::string platform;
    std::string relative_path;
};

struct PluginParameterDocument final {
    std::string name;
    std::string type;
    bool required{false};
    std::optional<std::string> default_value;
    std::optional<std::string> minimum;
    std::optional<std::string> maximum;
    std::vector<std::string> enum_values;
};

struct PluginInputPortDocument final {
    std::string name;
    bool required{false};
    std::vector<std::string> accepted_file_types;
};

struct PluginOutputPortDocument final {
    std::string name;
    std::string file_type;
};

struct PluginModuleDocument final {
    std::string id;
    std::string type;
    std::vector<PluginEntrypointDocument> entrypoints;
    std::vector<PluginParameterDocument> parameters{};
    std::vector<PluginInputPortDocument> inputs{};
    std::vector<PluginOutputPortDocument> outputs{};
};

struct PluginManifestDocument final {
    std::uint32_t manifest_version{current_plugin_manifest_version};
    std::string id;
    std::string name;
    std::string version;
    std::string api_version;
    std::string publisher;
    std::vector<PluginModuleDocument> modules;
};

struct PluginInvocationParameterDocument final {
    std::string name;
    std::string type;
    std::string value;
};

struct PluginInvocationInputDocument final {
    std::string port;
    std::string source_kind;
    std::string source_id;
    std::string file_type;
    std::string path;
};

struct PluginInvocationOutputDocument final {
    std::string port;
    std::string file_type;
    std::string path;
};

struct PluginInvocationDocument final {
    std::uint32_t schema_version{current_plugin_invocation_schema_version};
    std::string job_id;
    std::int64_t job_revision{0};
    std::string step_id;
    std::string module_id;
    std::vector<PluginInvocationParameterDocument> parameters;
    std::vector<PluginInvocationInputDocument> inputs;
    std::vector<PluginInvocationOutputDocument> outputs;
};

}  // namespace biocore::plugin_protocol
