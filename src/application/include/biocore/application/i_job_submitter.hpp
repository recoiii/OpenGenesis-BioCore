#pragma once

#include <optional>
#include <string>

#include "biocore/application/pipeline_bindings.hpp"
#include "biocore/domain/job.hpp"
#include "biocore/domain/job_priority.hpp"

namespace biocore::application {

struct SubmitJobRequest final {
    std::optional<std::string> analysis_id;
    std::string pipeline_id;
    std::string pipeline_version;
    domain::JobPriority priority{domain::JobPriority::normal};
    PipelineRunBindings bindings{};
};

class IJobSubmitter {
public:
    virtual ~IJobSubmitter() = default;
    [[nodiscard]] virtual domain::Job submit(const SubmitJobRequest& request) = 0;
};

}  // namespace biocore::application
