#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <spawn.h>
#include <unistd.h>
extern char** environ;
#endif

#include "biocore/worker_protocol/launch_arguments.hpp"

[[nodiscard]] std::uint64_t current_process_id() noexcept {
#if defined(_WIN32)
    return static_cast<std::uint64_t>(GetCurrentProcessId());
#else
    return static_cast<std::uint64_t>(::getpid());
#endif
}

int run_tree_child(const std::filesystem::path& pid_path) {
    std::ofstream output{pid_path, std::ios::binary | std::ios::trunc};
    if (!output) return 5;
    output << current_process_id() << '\n';
    output.close();
    if (!output) return 6;
    std::this_thread::sleep_for(std::chrono::seconds{30});
    return EXIT_SUCCESS;
}

#if defined(_WIN32)
[[nodiscard]] std::wstring quote_windows_argument(const std::wstring& value) {
    return L"\"" + value + L"\"";
}

int spawn_tree_child(
    const std::filesystem::path& self,
    const std::filesystem::path& pid_path
) {
    std::wstring command_line = quote_windows_argument(self.native()) + L" --tree-child " +
                                quote_windows_argument(pid_path.native());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(
            self.c_str(), command_line.data(), nullptr, nullptr, FALSE, 0U,
            nullptr, nullptr, &startup, &process
        ) == FALSE) {
        return 7;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return EXIT_SUCCESS;
}
#else
int spawn_tree_child(
    const std::filesystem::path& self,
    const std::filesystem::path& pid_path
) {
    std::string executable = self.string();
    std::string pid = pid_path.string();
    std::array<char*, 4U> arguments{
        executable.data(), const_cast<char*>("--tree-child"), pid.data(), nullptr,
    };
    pid_t child_pid = -1;
    const int result = ::posix_spawn(
        &child_pid, self.c_str(), nullptr, nullptr, arguments.data(), environ
    );
    return result == 0 ? EXIT_SUCCESS : 7;
}
#endif

[[nodiscard]] std::filesystem::path path_from_utf8(const std::string_view value) {
    const std::u8string utf8{
        reinterpret_cast<const char8_t*>(value.data()),
        reinterpret_cast<const char8_t*>(value.data() + value.size())
    };
    return std::filesystem::path{utf8};
}

int main(const int argc, const char* const argv[]) {
    if (argc == 3 && std::string_view{argv[1]} == "--tree-child") {
        return run_tree_child(path_from_utf8(argv[2]));
    }
    if (argc != 7) {
        return 2;
    }

    const std::array<std::string_view, 6U> raw_arguments{
        argv[1], argv[2], argv[3], argv[4], argv[5], argv[6],
    };

    try {
        const auto arguments = biocore::worker_protocol::parse_launch_arguments(raw_arguments);
        const std::filesystem::path project_root =
            path_from_utf8(arguments.project_root);
        const std::filesystem::path output_path =
            project_root / ("worker-probe-" + std::to_string(arguments.job_revision) + ".txt");

        std::ofstream output{output_path, std::ios::binary | std::ios::trunc};
        if (!output) {
            return 3;
        }
        output << "job_id=" << arguments.job_id << '\n'
               << "project_root=" << arguments.project_root << '\n'
               << "job_revision=" << arguments.job_revision << '\n';
        output.close();
        if (!output) {
            return 4;
        }

        if (arguments.job_id == "probe-malformed") {
            std::cout << "{malformed-json}\n";
            std::cerr << "probe diagnostic line\n";
            std::cout.flush();
            std::cerr.flush();
        }
        if (arguments.job_id == "probe-oversized") {
            std::cout << std::string(70U * 1024U, 'x');
            std::cout.flush();
        }
        if (arguments.job_id == "probe-flood") {
            constexpr std::size_t line_count = 2048U;
            constexpr std::size_t line_payload = 1023U;
            std::string flood;
            flood.reserve(line_count * (line_payload + 1U));
            const std::string line(line_payload, 'd');
            for (std::size_t index = 0U; index < line_count; ++index) {
                flood += line;
                flood.push_back('\n');
            }
            std::cerr.write(flood.data(), static_cast<std::streamsize>(flood.size()));
            std::cerr.flush();
        }

        if (arguments.job_id == "probe-tree-hang") {
            const std::filesystem::path self = std::filesystem::canonical(path_from_utf8(argv[0]));
            const std::filesystem::path child_pid_path = project_root / "process-tree-child.pid";
            const int spawn_result = spawn_tree_child(self, child_pid_path);
            if (spawn_result != EXIT_SUCCESS) return spawn_result;
            std::this_thread::sleep_for(std::chrono::seconds{30});
        } else if (arguments.job_id == "probe-hang") {
            std::this_thread::sleep_for(std::chrono::seconds{30});
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds{300});
        }
        if (arguments.job_id == "probe-exit-23") {
            return 23;
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
