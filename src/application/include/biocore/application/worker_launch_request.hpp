#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "biocore/domain/job_priority.hpp"

namespace biocore::application {

struct WorkerLaunchRequest final {
    std::string job_id;
    std::optional<std::string> analysis_id;
    std::optional<std::string> pipeline_id;
    std::optional<std::string> pipeline_version;
    domain::JobPriority priority{domain::JobPriority::normal};
    std::int64_t job_revision{0};
    std::optional<std::string> execution_plan_path;
};

}  // namespace biocore::application
