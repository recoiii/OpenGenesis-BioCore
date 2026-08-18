#pragma once

#include <filesystem>
#include <string>

#include "biocore/application/execution_plan.hpp"

namespace biocore::infrastructure {

class JsonPluginInvocationStore final {
public:
    explicit JsonPluginInvocationStore(std::filesystem::path project_root);

    [[nodiscard]] std::string store(
        std::string_view job_id,
        std::int64_t job_revision,
        const application::ExecutionPlanStep& step
    );

private:
    std::filesystem::path project_root_;
};

}  // namespace biocore::infrastructure
