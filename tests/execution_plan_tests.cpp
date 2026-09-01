#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "biocore/application/execution_plan.hpp"
#include "biocore/application/i_execution_plan_store.hpp"
#include "biocore/application/pipeline_planner.hpp"
#include "biocore/application/pipeline_preparation_service.hpp"
#include "biocore/domain/pipeline_definition.hpp"
#include "biocore/domain/pipeline_step.hpp"
#include "plugin_test_support.hpp"

namespace {

using biocore::application::ExecutionPlan;
using biocore::application::IExecutionPlanStore;
using biocore::application::PipelinePlanner;
using biocore::application::PipelinePreparationService;
using biocore::domain::PipelineDefinition;
using biocore::domain::PipelineStep;
using biocore::tests::FakePluginRegistry;

class FakeStore final : public IExecutionPlanStore {
public:
    std::string store(const ExecutionPlan& plan) override {
        ++calls;
        observed_job_id = std::string{plan.job_id()};
        return "/tmp/execution-plan.json";
    }
    std::string observed_job_id;
    int calls{0};
    void discard(std::string_view snapshot_path) override {
        discarded.push_back(std::string{snapshot_path});
    }
    std::vector<std::string> discarded;

};


class EmptyRegistry final : public biocore::application::IPluginRegistry {
public:
    [[nodiscard]] std::optional<biocore::application::ResolvedPluginModule> find_module(
        std::string_view
    ) const override {
        return std::nullopt;
    }
    [[nodiscard]] std::vector<biocore::application::RegisteredPlugin> list_plugins() const override {
        return {};
    }
};

template <typename Function>
[[nodiscard]] bool rejects(Function&& function) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

[[nodiscard]] PipelineDefinition definition() {
    return PipelineDefinition{
        2U,
        "org.biocore.demo",
        "Demo",
        "1.0.0",
        {
            PipelineStep{"validate", "org.biocore.demo.validate", "0.1.0", {}, 2.0},
            PipelineStep{"scan", "org.biocore.demo.scan", "0.1.0", {"validate"}, 6.0},
            PipelineStep{"report", "org.biocore.demo.report", "0.1.0", {"scan"}, 2.0},
        },
    };
}

[[nodiscard]] bool planning_and_progress_contract() {
    const FakePluginRegistry registry;
    const ExecutionPlan plan = PipelinePlanner::create_execution_plan(
        definition(), "job-1", 7, registry
    );
    if (plan.job_id() != "job-1" || plan.job_revision() != 7 || plan.steps().size() != 3U ||
        plan.steps()[0].id != "validate" || plan.steps()[1].id != "scan" ||
        plan.steps()[2].id != "report" ||
        std::abs(plan.steps()[0].normalized_weight - 0.2) > 1.0e-12 ||
        std::abs(plan.steps()[1].normalized_weight - 0.6) > 1.0e-12 ||
        std::abs(plan.steps()[2].normalized_weight - 0.2) > 1.0e-12 ||
        plan.steps()[0].plugin_id != "org.biocore.demo" ||
        plan.steps()[0].plugin_version != "0.1.0" ||
        plan.steps()[0].plugin_manifest_version != 2U ||
        plan.steps()[0].plugin_api_version != "1.0" ||
        plan.steps()[0].executable_path.empty()) {
        return false;
    }
    const double first = plan.calculate_overall_progress({{"validate", 1.0}});
    const double second = plan.calculate_overall_progress({{"validate", 1.0}, {"scan", 0.5}});
    const double completed = plan.calculate_overall_progress(
        {{"validate", 1.0}, {"scan", 1.0}, {"report", 1.0}}
    );
    return std::abs(first - 0.2) < 1.0e-12 && std::abs(second - 0.5) < 1.0e-12 &&
           std::abs(completed - 1.0) < 1.0e-12;
}

[[nodiscard]] bool progress_rejection_contract() {
    const FakePluginRegistry registry;
    const ExecutionPlan plan = PipelinePlanner::create_execution_plan(
        definition(), "job-1", 0, registry
    );
    return rejects([&] { static_cast<void>(plan.calculate_overall_progress({{"unknown", 1.0}})); }) &&
           rejects([&] { static_cast<void>(plan.calculate_overall_progress({{"validate", 1.1}})); }) &&
           rejects([&] { static_cast<void>(plan.calculate_overall_progress({{"scan", 0.1}})); });
}

[[nodiscard]] bool preparation_service_contract() {
    FakeStore store;
    const FakePluginRegistry registry;
    PipelinePreparationService service{store, registry};
    const auto prepared = service.prepare(definition(), "job-prepare", 3);
    return prepared.snapshot_path == "/tmp/execution-plan.json" &&
           prepared.plan.job_id() == "job-prepare" && store.observed_job_id == "job-prepare";
}

[[nodiscard]] bool unavailable_module_contract() {
    FakeStore store;
    const EmptyRegistry registry;
    PipelinePreparationService service{store, registry};
    return rejects([&] {
               static_cast<void>(service.prepare(definition(), "job-missing", 0));
           }) && store.calls == 0;
}

}  // namespace

int main() {
    const bool passed = planning_and_progress_contract() && progress_rejection_contract() &&
                        preparation_service_contract() && unavailable_module_contract();
    if (!passed) {
        std::cerr << "Execution plan tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Execution plan tests passed\n";
    return EXIT_SUCCESS;
}
