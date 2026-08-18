#include "biocore/domain/job_priority.hpp"

namespace biocore::domain {

std::string_view to_string(const JobPriority priority) noexcept {
    switch (priority) {
        case JobPriority::low:
            return "low";
        case JobPriority::normal:
            return "normal";
        case JobPriority::high:
            return "high";
    }

    return "unknown";
}

std::optional<JobPriority> job_priority_from_string(const std::string_view value) noexcept {
    if (value == "low") {
        return JobPriority::low;
    }
    if (value == "normal") {
        return JobPriority::normal;
    }
    if (value == "high") {
        return JobPriority::high;
    }
    return std::nullopt;
}

}  // namespace biocore::domain
