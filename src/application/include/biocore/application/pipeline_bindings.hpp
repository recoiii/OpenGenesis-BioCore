#pragma once

#include <string>
#include <variant>
#include <vector>

#include "biocore/domain/plugin_io_contract.hpp"

namespace biocore::application {

struct PipelineParameterBinding final {
    std::string name;
    domain::PluginParameterValue value;
};

struct ManagedFileInputSource final {
    std::string file_id;
};

struct StepOutputInputSource final {
    std::string step_id;
    std::string output_port;
};

using PipelineInputSource = std::variant<ManagedFileInputSource, StepOutputInputSource>;

struct PipelineInputBinding final {
    std::string port_name;
    PipelineInputSource source;
};

struct PipelineStepBindings final {
    std::string step_id;
    std::vector<PipelineParameterBinding> parameters;
    std::vector<PipelineInputBinding> inputs;
};

struct PipelineRunBindings final {
    std::vector<PipelineStepBindings> steps;
};

}  // namespace biocore::application
