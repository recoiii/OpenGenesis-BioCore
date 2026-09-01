#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "biocore/domain/pipeline_definition.hpp"
#include "biocore/domain/pipeline_step.hpp"

namespace {

using biocore::domain::PipelineDefinition;
using biocore::domain::PipelineStep;

template <typename Function>
[[nodiscard]] bool rejects(Function&& function) {
    try {
        function();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

[[nodiscard]] PipelineDefinition make_valid() {
    return PipelineDefinition{
        2U,
        "org.biocore.demo",
        "Demo pipeline",
        "1.0.0",
        {
            PipelineStep{"validate", "org.biocore.demo.validate", "0.1.0", {}, 2.0},
            PipelineStep{"scan", "org.biocore.demo.scan", "0.1.0", {"validate"}, 6.0},
            PipelineStep{"report", "org.biocore.demo.report", "0.1.0", {"scan"}, 2.0},
        },
    };
}

[[nodiscard]] bool valid_dag_contract() {
    const PipelineDefinition definition = make_valid();
    return definition.schema_version() == 2U && definition.id() == "org.biocore.demo" &&
           definition.steps().size() == 3U &&
           definition.steps()[0].plugin_version() == "0.1.0" &&
           definition.topological_order() == std::vector<std::size_t>{0U, 1U, 2U} &&
           std::abs(definition.total_weight() - 10.0) < 1.0e-12;
}

[[nodiscard]] bool deterministic_parallel_order_contract() {
    const PipelineDefinition definition{
        2U,
        "org.biocore.parallel",
        "Parallel",
        "1.0.0",
        {
            PipelineStep{"root-b", "org.biocore.demo.module-b", "0.1.0", {}, 1.0},
            PipelineStep{"root-a", "org.biocore.demo.module-a", "0.1.0", {}, 1.0},
            PipelineStep{"join", "org.biocore.demo.module-join", "0.1.0", {"root-a", "root-b"}, 1.0},
        },
    };
    return definition.topological_order() == std::vector<std::size_t>{0U, 1U, 2U};
}

[[nodiscard]] bool step_invariant_contract() {
    return rejects([] { static_cast<void>(PipelineStep{"", "org.biocore.demo.module", "0.1.0", {}, 1.0}); }) &&
           rejects([] { static_cast<void>(PipelineStep{"step", "", "0.1.0", {}, 1.0}); }) &&
           rejects([] { static_cast<void>(PipelineStep{"step", "org.biocore.demo.module", "1", {}, 1.0}); }) &&
           rejects([] { static_cast<void>(PipelineStep{"step", "org.biocore.demo.module", "0.1.0", {}, 0.0}); }) &&
           rejects([] { static_cast<void>(PipelineStep{"step", "org.biocore.demo.module", "0.1.0", {"step"}, 1.0}); }) &&
           rejects([] {
               static_cast<void>(PipelineStep{"step", "org.biocore.demo.module", "0.1.0", {"a", "a"}, 1.0});
           });
}

[[nodiscard]] bool graph_rejection_contract() {
    return rejects([] {
               static_cast<void>(PipelineDefinition{
                   3U, "org.biocore.pipeline", "Pipeline", "1.0.0",
                   {PipelineStep{"a", "org.biocore.demo.m", "0.1.0", {}, 1.0}}
               });
           }) &&
           rejects([] {
               static_cast<void>(PipelineDefinition{
                   2U, "Pipeline", "Pipeline", "1.0.0",
                   {PipelineStep{"a", "org.biocore.demo.m", "0.1.0", {}, 1.0}}
               });
           }) &&
           rejects([] {
               static_cast<void>(PipelineDefinition{
                   2U, "org.biocore.pipeline", "Pipeline", "1",
                   {PipelineStep{"a", "org.biocore.demo.m", "0.1.0", {}, 1.0}}
               });
           }) &&
           rejects([] {
               static_cast<void>(PipelineDefinition{2U, "org.biocore.pipeline", "Pipeline", "1.0.0", {}});
           }) &&
           rejects([] {
               static_cast<void>(PipelineDefinition{
                   2U, "org.biocore.pipeline", "Pipeline", "1.0.0",
                   {PipelineStep{"a", "org.biocore.demo.m", "0.1.0", {}, 1.0}, PipelineStep{"a", "org.biocore.demo.m", "0.1.0", {}, 1.0}},
               });
           }) &&
           rejects([] {
               static_cast<void>(PipelineDefinition{
                   2U, "org.biocore.pipeline", "Pipeline", "1.0.0",
                   {PipelineStep{"a", "org.biocore.demo.m", "0.1.0", {"missing"}, 1.0}},
               });
           }) &&
           rejects([] {
               static_cast<void>(PipelineDefinition{
                   2U,
                   "org.biocore.pipeline",
                   "Pipeline",
                   "1.0.0",
                   {
                       PipelineStep{"a", "org.biocore.demo.m", "0.1.0", {"b"}, 1.0},
                       PipelineStep{"b", "org.biocore.demo.m", "0.1.0", {"a"}, 1.0},
                   },
               });
           });
}

}  // namespace

int main() {
    const bool passed = valid_dag_contract() && deterministic_parallel_order_contract() &&
                        step_invariant_contract() && graph_rejection_contract();
    if (!passed) {
        std::cerr << "Pipeline definition tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Pipeline definition tests passed\n";
    return EXIT_SUCCESS;
}
