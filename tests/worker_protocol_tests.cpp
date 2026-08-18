#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <stdexcept>
#include <string_view>

#include "biocore/worker_protocol/launch_arguments.hpp"
#include "biocore/worker_protocol/protocol.hpp"

namespace {

template <std::size_t Size>
[[nodiscard]] bool rejects(const std::array<std::string_view, Size>& arguments) {
    try {
        static_cast<void>(biocore::worker_protocol::parse_launch_arguments(arguments));
        return false;
    } catch (const std::invalid_argument&) {
        return true;
    }
}

[[nodiscard]] bool test_launch_argument_contract() {
    constexpr std::array valid{
        std::string_view{"--job-id"},
        std::string_view{"job;$(not-a-shell) quoted value"},
        std::string_view{"--project-root"},
        std::string_view{"/tmp/project with spaces"},
        std::string_view{"--job-revision"},
        std::string_view{"42"},
    };
    const auto parsed = biocore::worker_protocol::parse_launch_arguments(valid);
    if (parsed.job_id != valid[1] || parsed.project_root != valid[3] ||
        parsed.job_revision != 42 || parsed.execution_plan_path.has_value()) {
        return false;
    }

    constexpr std::array with_plan{
        std::string_view{"--job-id"},
        std::string_view{"job-plan"},
        std::string_view{"--project-root"},
        std::string_view{"/tmp/project"},
        std::string_view{"--job-revision"},
        std::string_view{"7"},
        std::string_view{"--execution-plan"},
        std::string_view{"/tmp/project/.biocore/runtime/jobs/job-plan/plan.json"},
    };
    const auto parsed_plan = biocore::worker_protocol::parse_launch_arguments(with_plan);
    if (parsed_plan.job_revision != 7 ||
        parsed_plan.execution_plan_path != std::optional<std::string>{with_plan[7]}) {
        return false;
    }

    constexpr std::array missing{
        std::string_view{"--job-id"}, std::string_view{"job"},
    };
    constexpr std::array wrong_order{
        std::string_view{"--project-root"},
        std::string_view{"/tmp/project"},
        std::string_view{"--job-id"},
        std::string_view{"job"},
        std::string_view{"--job-revision"},
        std::string_view{"0"},
    };
    constexpr std::array blank_job{
        std::string_view{"--job-id"},
        std::string_view{"   "},
        std::string_view{"--project-root"},
        std::string_view{"/tmp/project"},
        std::string_view{"--job-revision"},
        std::string_view{"0"},
    };
    constexpr std::array negative_revision{
        std::string_view{"--job-id"},
        std::string_view{"job"},
        std::string_view{"--project-root"},
        std::string_view{"/tmp/project"},
        std::string_view{"--job-revision"},
        std::string_view{"-1"},
    };
    constexpr std::array overflow_revision{
        std::string_view{"--job-id"},
        std::string_view{"job"},
        std::string_view{"--project-root"},
        std::string_view{"/tmp/project"},
        std::string_view{"--job-revision"},
        std::string_view{"9223372036854775808"},
    };
    constexpr std::array trailing_revision{
        std::string_view{"--job-id"},
        std::string_view{"job"},
        std::string_view{"--project-root"},
        std::string_view{"/tmp/project"},
        std::string_view{"--job-revision"},
        std::string_view{"1x"},
    };

    constexpr std::array blank_plan{
        std::string_view{"--job-id"}, std::string_view{"job"},
        std::string_view{"--project-root"}, std::string_view{"/tmp/project"},
        std::string_view{"--job-revision"}, std::string_view{"0"},
        std::string_view{"--execution-plan"}, std::string_view{"   "},
    };

    return rejects(missing) && rejects(wrong_order) && rejects(blank_job) &&
           rejects(negative_revision) && rejects(overflow_revision) &&
           rejects(trailing_revision) && rejects(blank_plan);
}

}  // namespace

int main() {
    using biocore::worker_protocol::MessageType;

    if (biocore::worker_protocol::current_protocol_version != 2U) {
        std::cerr << "Unexpected protocol version\n";
        return EXIT_FAILURE;
    }

    constexpr std::array cases{
        std::pair{MessageType::ready, std::string_view{"ready"}},
        std::pair{MessageType::heartbeat, std::string_view{"heartbeat"}},
        std::pair{MessageType::progress, std::string_view{"progress"}},
        std::pair{MessageType::log, std::string_view{"log"}},
        std::pair{MessageType::artifact, std::string_view{"artifact"}},
        std::pair{MessageType::completed, std::string_view{"completed"}},
        std::pair{MessageType::failed, std::string_view{"failed"}},
        std::pair{MessageType::cancel, std::string_view{"cancel"}},
        std::pair{MessageType::shutdown, std::string_view{"shutdown"}},
        std::pair{MessageType::ping, std::string_view{"ping"}},
        std::pair{MessageType::pong, std::string_view{"pong"}},
    };

    for (const auto& [type, expected] : cases) {
        if (biocore::worker_protocol::to_string(type) != expected) {
            std::cerr << "Unexpected protocol message name\n";
            return EXIT_FAILURE;
        }
    }

    if (!test_launch_argument_contract()) {
        std::cerr << "Worker launch argument contract failed\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
