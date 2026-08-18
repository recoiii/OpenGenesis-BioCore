#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace biocore::worker_protocol {

inline constexpr std::string_view job_id_argument = "--job-id";
inline constexpr std::string_view project_root_argument = "--project-root";
inline constexpr std::string_view job_revision_argument = "--job-revision";
inline constexpr std::string_view execution_plan_argument = "--execution-plan";

struct WorkerLaunchArguments final {
    std::string job_id;
    std::string project_root;
    std::int64_t job_revision{0};
    std::optional<std::string> execution_plan_path;
};

// Parses either the six base arguments or the eight execution-plan arguments following the
// executable name. Both fixed orderings are part of worker protocol v1 and intentionally avoid
// permissive or ambiguous CLI interpretation.
[[nodiscard]] WorkerLaunchArguments parse_launch_arguments(
    std::span<const std::string_view> arguments
);

}  // namespace biocore::worker_protocol
