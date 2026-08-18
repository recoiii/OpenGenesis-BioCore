#pragma once

#include <string_view>

namespace biocore::application {

class IPluginProcessRunner {
public:
    virtual ~IPluginProcessRunner() = default;

    [[nodiscard]] virtual int run(
        std::string_view executable_path,
        std::string_view plugin_root_path,
        std::string_view plugin_id,
        std::string_view plugin_version,
        std::string_view module_id,
        std::string_view step_id,
        std::string_view invocation_snapshot_path
    ) = 0;
};

}  // namespace biocore::application
