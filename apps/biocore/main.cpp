#include <charconv>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "local_server_bootstrap.hpp"
#include "project_init_bootstrap.hpp"
#include "biocore/application/build_info.hpp"
#include "biocore/application/job_scheduler.hpp"
#include "biocore/application/health_snapshot.hpp"
#include "biocore/infrastructure/system_clock.hpp"
#include "biocore/presentation/health_json.hpp"
#include "biocore/presentation/local_web_server.hpp"

namespace {
constexpr std::string_view health_argument = "--health";
constexpr std::string_view version_argument = "--version";
constexpr std::string_view serve_argument = "--serve";
constexpr std::string_view init_project_argument = "--init-project";
constexpr std::string_view name_argument = "--name";
constexpr std::string_view catalog_argument = "--catalog";
constexpr std::string_view port_argument = "--port";
constexpr std::string_view maximum_concurrent_jobs_argument = "--max-concurrent-jobs";

void print_usage() {
    std::cout << "OpenGenesis-BioCore local-first executable\n"
              << "Usage:\n"
              << "  biocore --health\n"
              << "  biocore --version\n"
              << "  biocore --init-project <project-root> --name <name> --catalog <catalog.sqlite>\n"
              << "  biocore --serve <project-root> [--port <1-65535>] "
                 "[--max-concurrent-jobs <1-64>]\n";
}

[[nodiscard]] std::uint16_t parse_port(const std::string_view value) {
    unsigned int parsed = 0U;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed == 0U || parsed > 65535U) {
        throw std::invalid_argument("Server port must be an integer between 1 and 65535");
    }
    return static_cast<std::uint16_t>(parsed);
}

[[nodiscard]] std::size_t parse_maximum_concurrent_jobs(const std::string_view value) {
    std::size_t parsed = 0U;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
        parsed == 0U ||
        parsed > biocore::application::JobScheduler::maximum_supported_concurrent_jobs) {
        throw std::invalid_argument(
            "Maximum concurrent jobs must be an integer between 1 and " +
            std::to_string(biocore::application::JobScheduler::maximum_supported_concurrent_jobs)
        );
    }
    return parsed;
}

[[nodiscard]] biocore::bootstrap::ProjectInitArguments parse_init_project_arguments(
    const int argc,
    const char* const argv[]
) {
    if (argc != 7 || std::string_view{argv[3]} != name_argument ||
        std::string_view{argv[5]} != catalog_argument) {
        throw std::invalid_argument("Invalid --init-project arguments");
    }
    return {
        .project_root = argv[2],
        .project_name = argv[4],
        .catalog_database_path = argv[6],
    };
}

[[nodiscard]] biocore::bootstrap::ServeArguments parse_serve_arguments(const int argc, const char* const argv[]) {
    if (argc < 3 || argc > 7 || ((argc - 3) % 2) != 0) {
        throw std::invalid_argument("Invalid --serve arguments");
    }
    biocore::bootstrap::ServeArguments arguments{
        .project_root = argv[2],
        .port = 8421U,
        .executable_path = argv[0],
        .pipeline_root = std::nullopt,
        .plugin_root = std::nullopt,
        .worker_executable = std::nullopt,
        .frontend_root = std::nullopt,
        .maximum_concurrent_jobs = biocore::application::JobScheduler::default_maximum_concurrent_jobs,
    };
    bool port_seen = false;
    bool concurrency_seen = false;
    for (int index = 3; index < argc; index += 2) {
        const std::string_view option{argv[index]};
        const std::string_view value{argv[index + 1]};
        if (option == port_argument && !port_seen) {
            arguments.port = parse_port(value);
            port_seen = true;
            continue;
        }
        if (option == maximum_concurrent_jobs_argument && !concurrency_seen) {
            arguments.maximum_concurrent_jobs = parse_maximum_concurrent_jobs(value);
            concurrency_seen = true;
            continue;
        }
        throw std::invalid_argument("Unknown or duplicate --serve option");
    }
    return arguments;
}
}

int main(const int argc, const char* const argv[]) {
    try {
        if (argc == 2) {
            const std::string_view argument{argv[1]};
            if (argument == version_argument) {
                std::cout << biocore::application::BuildInfo::version() << '\n';
                return 0;
            }
            if (argument == health_argument) {
                biocore::infrastructure::SystemClock clock;
                const biocore::application::HealthSnapshot snapshot{
                    .status = "healthy",
                    .component = "biocore-bootstrap",
                    .version = std::string{biocore::application::BuildInfo::version()},
                    .timestamp_utc = clock.now_utc_iso8601(),
                };
                std::cout << biocore::presentation::render_health_json(snapshot) << '\n';
                return 0;
            }
        }
        if (argc >= 2 && std::string_view{argv[1]} == init_project_argument) {
            static_cast<void>(biocore::bootstrap::initialize_project(
                parse_init_project_arguments(argc, argv), std::cout
            ));
            return 0;
        }
        if (argc >= 3 && std::string_view{argv[1]} == serve_argument) {
            auto server = biocore::presentation::make_local_web_server();
            return biocore::bootstrap::run_local_server(
                parse_serve_arguments(argc, argv), *server, std::cout, std::cerr
            );
        }
        print_usage();
        return argc == 1 ? 0 : 2;
    } catch (const std::exception& exception) {
        std::cerr << "OpenGenesis-BioCore bootstrap failure: " << exception.what() << '\n';
        return 1;
    }
}
