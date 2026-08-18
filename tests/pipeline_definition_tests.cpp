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
        1U,
        "org.biocore.demo",
        "Demo pipeline",
        "1.0.0",
        {
            PipelineStep{"validate", "org.biocore.demo.validate", {}, 2.0},
            PipelineStep{"scan", "org.biocore.demo.scan", {"validate"}, 6.0},
            PipelineStep{"report", "org.biocore.demo.report", {"scan"}, 2.0},
        },
    };
}

[[nodiscard]] bool valid_dag_contract() {
    const PipelineDefinition definition = make_valid();
    return definition.schema_version() == 1U && definition.id() == "org.biocore.demo" &&
           definition.steps().size() == 3U &&
           definition.topological_order() == std::vector<std::size_t>{0U, 1U, 2U} &&
           std::abs(definition.total_weight() - 10.0) < 1.0e-12;
}

[[nodiscard]] bool deterministic_parallel_order_contract() {
    const PipelineDefinition definition{
        1U,
        "parallel",
        "Parallel",
        "1",
        {
            PipelineStep{"root-b", "module-b", {}, 1.0},
            PipelineStep{"root-a", "module-a", {}, 1.0},
            PipelineStep{"join", "module-join", {"root-a", "root-b"}, 1.0},
        },
    };
    return definition.topological_order() == std::vector<std::size_t>{0U, 1U, 2U};
}

[[nodiscard]] bool step_invariant_contract() {
    return rejects([] { static_cast<void>(PipelineStep{"", "module", {}, 1.0}); }) &&
           rejects([] { static_cast<void>(PipelineStep{"step", "", {}, 1.0}); }) &&
           rejects([] { static_cast<void>(PipelineStep{"step", "module", {}, 0.0}); }) &&
           rejects([] { static_cast<void>(PipelineStep{"step", "module", {"step"}, 1.0}); }) &&
           rejects([] {
               static_cast<void>(PipelineStep{"step", "module", {"a", "a"}, 1.0});
           });
}

[[nodiscard]] bool graph_rejection_contract() {
    return rejects([] {
               static_cast<void>(PipelineDefinition{
                   2U, "pipeline", "Pipeline", "1", {PipelineStep{"a", "m", {}, 1.0}}
               });
           }) &&
           rejects([] {
               static_cast<void>(PipelineDefinition{1U, "pipeline", "Pipeline", "1", {}});
           }) &&
           rejects([] {
               static_cast<void>(PipelineDefinition{
                   1U,
                   "pipeline",
                   "Pipeline",
                   "1",
                   {PipelineStep{"a", "m", {}, 1.0}, PipelineStep{"a", "m", {}, 1.0}},
               });
           }) &&
           rejects([] {
               static_cast<void>(PipelineDefinition{
                   1U,
                   "pipeline",
                   "Pipeline",
                   "1",
                   {PipelineStep{"a", "m", {"missing"}, 1.0}},
               });
           }) &&
           rejects([] {
               static_cast<void>(PipelineDefinition{
                   1U,
                   "pipeline",
                   "Pipeline",
                   "1",
                   {
                       PipelineStep{"a", "m", {"b"}, 1.0},
                       PipelineStep{"b", "m", {"a"}, 1.0},
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
