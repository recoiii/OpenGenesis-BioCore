#pragma once

#include "biocore/application/i_plugin_process_runner.hpp"

namespace biocore::infrastructure {

class PlatformPluginProcessRunner final : public application::IPluginProcessRunner {
public:
    [[nodiscard]] int run(
        std::string_view executable_path,
        std::string_view plugin_root_path,
        std::string_view plugin_id,
        std::string_view plugin_version,
        std::string_view module_id,
        std::string_view step_id,
        std::string_view invocation_snapshot_path
    ) override;
};

}  // namespace biocore::infrastructure
