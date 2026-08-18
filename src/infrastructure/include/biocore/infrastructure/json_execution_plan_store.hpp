#pragma once

#include <filesystem>
#include <string_view>

#include "biocore/application/i_execution_plan_store.hpp"

namespace biocore::infrastructure {

class JsonExecutionPlanStore final : public application::IExecutionPlanStore {
public:
    explicit JsonExecutionPlanStore(std::filesystem::path project_root);

    [[nodiscard]] std::string store(const application::ExecutionPlan& plan) override;
    void discard(std::string_view snapshot_path) override;

private:
    std::filesystem::path project_root_;
};

}  // namespace biocore::infrastructure
