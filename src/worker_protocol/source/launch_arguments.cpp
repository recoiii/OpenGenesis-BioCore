#include "biocore/worker_protocol/launch_arguments.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <stdexcept>
#include <utility>
#include <system_error>

namespace biocore::worker_protocol {
namespace {

constexpr std::size_t base_argument_count = 6U;
constexpr std::size_t execution_plan_argument_count = 8U;
constexpr std::size_t maximum_job_id_length = 128U;
constexpr std::size_t maximum_project_root_length = 32U * 1024U;
constexpr std::size_t maximum_execution_plan_path_length = 32U * 1024U;

[[nodiscard]] bool is_blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
               return std::isspace(static_cast<unsigned char>(character)) != 0;
           });
}

void require_text(
    const std::string_view value,
    const std::string_view field_name,
    const std::size_t maximum_length
) {
    if (is_blank(value)) {
        throw std::invalid_argument(std::string{field_name} + " must not be blank");
    }
    if (value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string{field_name} + " must not contain NUL characters");
    }
    if (value.size() > maximum_length) {
        throw std::invalid_argument(std::string{field_name} + " exceeds the maximum length");
    }
}

[[nodiscard]] std::int64_t parse_revision(const std::string_view value) {
    if (value.empty()) {
        throw std::invalid_argument("Worker job revision must not be blank");
    }

    std::int64_t revision = 0;
    const char* const begin = value.data();
    const char* const end = value.data() + value.size();
    const auto [position, error] = std::from_chars(begin, end, revision);
    if (error != std::errc{} || position != end || revision < 0) {
        throw std::invalid_argument("Worker job revision must be a non-negative integer");
    }
    return revision;
}

}  // namespace

WorkerLaunchArguments parse_launch_arguments(
    const std::span<const std::string_view> arguments
) {
    if (arguments.size() != base_argument_count &&
        arguments.size() != execution_plan_argument_count) {
        throw std::invalid_argument("Worker launch requires exactly six or eight arguments");
    }
    if (arguments[0] != job_id_argument || arguments[2] != project_root_argument ||
        arguments[4] != job_revision_argument ||
        (arguments.size() == execution_plan_argument_count &&
         arguments[6] != execution_plan_argument)) {
        throw std::invalid_argument("Worker launch arguments are missing or out of order");
    }

    require_text(arguments[1], "Worker job id", maximum_job_id_length);
    require_text(arguments[3], "Worker project root", maximum_project_root_length);
    std::optional<std::string> execution_plan_path;
    if (arguments.size() == execution_plan_argument_count) {
        require_text(
            arguments[7], "Worker execution plan path", maximum_execution_plan_path_length
        );
        execution_plan_path = std::string{arguments[7]};
    }

    return WorkerLaunchArguments{
        .job_id = std::string{arguments[1]},
        .project_root = std::string{arguments[3]},
        .job_revision = parse_revision(arguments[5]),
        .execution_plan_path = std::move(execution_plan_path),
    };
}

}  // namespace biocore::worker_protocol
