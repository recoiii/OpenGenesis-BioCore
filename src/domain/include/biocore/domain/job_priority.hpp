#pragma once

#include <optional>
#include <string_view>

namespace biocore::domain {

enum class JobPriority {
    low,
    normal,
    high
};

[[nodiscard]] std::string_view to_string(JobPriority priority) noexcept;
[[nodiscard]] std::optional<JobPriority> job_priority_from_string(std::string_view value) noexcept;

}  // namespace biocore::domain
