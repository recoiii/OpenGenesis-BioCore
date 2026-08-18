#pragma once

#include <string>
#include <string_view>

#include "biocore/plugin_protocol/plugin_document.hpp"

namespace biocore::plugin_protocol {

[[nodiscard]] PluginManifestDocument parse_plugin_manifest_document(std::string_view json);
[[nodiscard]] std::string serialize_plugin_manifest_document(
    const PluginManifestDocument& document
);

[[nodiscard]] PluginInvocationDocument parse_plugin_invocation_document(std::string_view json);
[[nodiscard]] std::string serialize_plugin_invocation_document(
    const PluginInvocationDocument& document
);

}  // namespace biocore::plugin_protocol
