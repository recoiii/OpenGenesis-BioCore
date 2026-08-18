#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#endif

#include "biocore/application/worker_launch_request.hpp"
#include "biocore/domain/job_priority.hpp"
#include "biocore/infrastructure/platform_worker_supervisor.hpp"

namespace {

namespace fs = std::filesystem;
using biocore::application::WorkerLaunchRequest;
using biocore::domain::JobPriority;
using biocore::infrastructure::PlatformWorkerSupervisor;
using biocore::infrastructure::WorkerProcessExit;
using biocore::infrastructure::WorkerProcessOutput;
using biocore::infrastructure::WorkerSupervisorError;
using biocore::infrastructure::WorkerSupervisorErrorCode;

[[nodiscard]] fs::path path_from_utf8(const std::string_view value) {
    const std::u8string utf8{
        reinterpret_cast<const char8_t*>(value.data()),
        reinterpret_cast<const char8_t*>(value.data() + value.size())
    };
    return fs::path{utf8};
}

class TemporaryDirectory final {
public:
    explicit TemporaryDirectory(const std::string_view suffix) {
        const auto unique = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()
        );
        path_ = fs::temp_directory_path() /
                path_from_utf8("biocore worker supervisor " + std::string{suffix} + " " + unique);
        fs::create_directories(path_);
        path_ = fs::canonical(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }

    [[nodiscard]] const fs::path& path() const noexcept {
        return path_;
    }

private:
    fs::path path_;
};

class CurrentPathGuard final {
public:
    explicit CurrentPathGuard(const fs::path& target) : original_{fs::current_path()} {
        fs::current_path(target);
    }

    ~CurrentPathGuard() {
        std::error_code error;
        fs::current_path(original_, error);
    }

private:
    fs::path original_;
};

[[nodiscard]] WorkerLaunchRequest request(std::string job_id, const std::int64_t revision) {
    return WorkerLaunchRequest{
        .job_id = std::move(job_id),
        .analysis_id = std::nullopt,
        .pipeline_id = std::string{"pipeline with spaces"},
        .pipeline_version = std::string{"1.0.0"},
        .priority = JobPriority::normal,
        .job_revision = revision,
        .execution_plan_path = std::nullopt,
    };
}

[[nodiscard]] std::vector<WorkerProcessExit> await_exit(PlatformWorkerSupervisor& supervisor) {
    for (int attempt = 0; attempt < 300; ++attempt) {
        auto exits = supervisor.reap_exited();
        if (!exits.empty()) {
            return exits;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return {};
}

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const std::u8string value = path.u8string();
    return std::string{reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] std::string read_file(const fs::path& path) {
    std::ifstream input{path, std::ios::binary};
    return std::string{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}
    };
}

[[nodiscard]] bool test_launch_and_reap(const fs::path& probe_executable) {
    TemporaryDirectory project{"launch [quoted]"};
    const CurrentPathGuard current_path{project.path()};
    PlatformWorkerSupervisor supervisor{probe_executable, project.path()};

#if defined(_WIN32)
    const std::string job_id = "job & echo unsafe quoted \\\" value";
#else
    const std::string job_id = "job;touch shell-injection-sentinel $(echo unsafe) \\\" value";
#endif
    supervisor.launch(request(job_id, 42));

    const auto process = supervisor.find_process(job_id);
    if (!process.has_value() || process->process_id == 0U ||
        supervisor.tracked_processes().size() != 1U) {
        return false;
    }

    bool duplicate_rejected = false;
    try {
        supervisor.launch(request(job_id, 43));
    } catch (const WorkerSupervisorError& error) {
        duplicate_rejected =
            error.code() == WorkerSupervisorErrorCode::duplicate_active_job &&
            error.job_id() == job_id;
    }
    if (!duplicate_rejected) {
        return false;
    }

    const auto exits = await_exit(supervisor);
    if (exits.size() != 1U || exits.front().job_id != job_id ||
        exits.front().process_id != process->process_id || exits.front().exit_code != 0 ||
        supervisor.find_process(job_id).has_value() || !supervisor.tracked_processes().empty()) {
        return false;
    }

    const std::string expected =
        "job_id=" + job_id + "\nproject_root=" + path_to_utf8(project.path()) +
        "\njob_revision=42\n";
    if (read_file(project.path() / "worker-probe-42.txt") != expected) {
        return false;
    }
    if (fs::exists(project.path() / "shell-injection-sentinel")) {
        return false;
    }

    supervisor.launch(request(job_id, 43));
    const auto second_exits = await_exit(supervisor);
    return second_exits.size() == 1U && second_exits.front().exit_code == 0 &&
           fs::exists(project.path() / "worker-probe-43.txt");
}

[[nodiscard]] bool test_nonzero_exit_and_concurrent_duplicate(
    const fs::path& probe_executable
) {
    TemporaryDirectory project{"concurrent duplicate"};
    PlatformWorkerSupervisor supervisor{probe_executable, project.path()};

    constexpr int thread_count = 8;
    std::atomic<bool> start{false};
    std::atomic<int> successes{0};
    std::atomic<int> duplicates{0};
    std::vector<std::thread> threads;
    threads.reserve(thread_count);
    for (int index = 0; index < thread_count; ++index) {
        threads.emplace_back([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            try {
                supervisor.launch(request("probe-exit-23", 23));
                successes.fetch_add(1, std::memory_order_relaxed);
            } catch (const WorkerSupervisorError& error) {
                if (error.code() == WorkerSupervisorErrorCode::duplicate_active_job) {
                    duplicates.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    start.store(true, std::memory_order_release);
    for (std::thread& thread : threads) {
        thread.join();
    }

    if (successes.load() != 1 || duplicates.load() != thread_count - 1 ||
        supervisor.tracked_processes().size() != 1U) {
        return false;
    }
    const auto exits = await_exit(supervisor);
    return exits.size() == 1U && exits.front().job_id == "probe-exit-23" &&
           exits.front().exit_code == 23 && supervisor.tracked_processes().empty();
}

[[nodiscard]] bool test_destructor_reaps_completed_child(const fs::path& probe_executable) {
#if defined(_WIN32)
    static_cast<void>(probe_executable);
    return true;
#else
    TemporaryDirectory project{"destructor reap"};
    std::uint64_t process_id = 0U;
    {
        PlatformWorkerSupervisor supervisor{probe_executable, project.path()};
        supervisor.launch(request("destructor-reap", 88));
        const auto process = supervisor.find_process("destructor-reap");
        if (!process.has_value()) {
            return false;
        }
        process_id = process->process_id;
        std::this_thread::sleep_for(std::chrono::milliseconds{450});
    }

    int status = 0;
    errno = 0;
    const pid_t wait_result =
        ::waitpid(static_cast<pid_t>(process_id), &status, WNOHANG);
    return wait_result == -1 && errno == ECHILD;
#endif
}

[[nodiscard]] bool test_termination_does_not_overwrite_existing_exit(
    const fs::path& probe_executable
) {
    TemporaryDirectory project{"already exited termination"};
    PlatformWorkerSupervisor supervisor{probe_executable, project.path()};
    supervisor.launch(request("probe-exit-23", 231));
    std::this_thread::sleep_for(std::chrono::milliseconds{450});
    if (supervisor.terminate("probe-exit-23") !=
        biocore::application::WorkerTerminationResult::already_exited) {
        return false;
    }
    const auto exits = await_exit(supervisor);
    return exits.size() == 1U && exits.front().job_id == "probe-exit-23" &&
           exits.front().exit_code == 23;
}

[[nodiscard]] bool process_is_alive(const std::uint64_t process_id) {
#if defined(_WIN32)
    HANDLE handle = OpenProcess(
        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        static_cast<DWORD>(process_id)
    );
    if (handle == nullptr) {
        return GetLastError() == ERROR_ACCESS_DENIED;
    }
    const DWORD wait_result = WaitForSingleObject(handle, 0U);
    CloseHandle(handle);
    return wait_result == WAIT_TIMEOUT;
#else
    errno = 0;
    const int result = ::kill(static_cast<pid_t>(process_id), 0);
    return result == 0 || errno == EPERM;
#endif
}

[[nodiscard]] bool await_process_dead(const std::uint64_t process_id) {
    for (int attempt = 0; attempt < 300; ++attempt) {
        if (!process_is_alive(process_id)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return false;
}

[[nodiscard]] std::optional<std::uint64_t> await_child_pid(const fs::path& path) {
    for (int attempt = 0; attempt < 300; ++attempt) {
        if (fs::exists(path)) {
            try {
                const std::string text = read_file(path);
                const std::uint64_t pid = std::stoull(text);
                if (pid != 0U) return pid;
            } catch (...) {
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return std::nullopt;
}

[[nodiscard]] bool test_force_termination_kills_process_tree(
    const fs::path& probe_executable
) {
    TemporaryDirectory project{"process tree termination"};
    PlatformWorkerSupervisor supervisor{probe_executable, project.path()};
    supervisor.launch(request("probe-tree-hang", 138));

    const fs::path pid_path = project.path() / "process-tree-child.pid";
    const auto child_pid = await_child_pid(pid_path);
    if (!child_pid.has_value() || !process_is_alive(*child_pid)) {
        return false;
    }
    if (supervisor.terminate("probe-tree-hang") !=
        biocore::application::WorkerTerminationResult::requested) {
        return false;
    }
    const auto exits = await_exit(supervisor);
    if (exits.size() != 1U || exits.front().job_id != "probe-tree-hang" ||
        exits.front().exit_code != 137) {
        return false;
    }
    return await_process_dead(*child_pid) && supervisor.tracked_processes().empty();
}

[[nodiscard]] bool test_force_termination(const fs::path& probe_executable) {
    TemporaryDirectory project{"forced termination"};
    PlatformWorkerSupervisor supervisor{probe_executable, project.path()};
    supervisor.launch(request("probe-hang", 137));
    if (!supervisor.find_process("probe-hang").has_value()) {
        return false;
    }
    if (supervisor.terminate("missing-job") !=
        biocore::application::WorkerTerminationResult::not_found) {
        return false;
    }
    if (supervisor.terminate("probe-hang") !=
        biocore::application::WorkerTerminationResult::requested) {
        return false;
    }
    const auto exits = await_exit(supervisor);
    return exits.size() == 1U && exits.front().job_id == "probe-hang" &&
           exits.front().exit_code == 137 && supervisor.tracked_processes().empty();
}

[[nodiscard]] bool test_actual_worker_launch(const fs::path& worker_executable) {
    TemporaryDirectory project{"actual worker"};
    PlatformWorkerSupervisor supervisor{worker_executable, project.path()};
    supervisor.launch(request("actual-worker-job", 7));
    const auto process = supervisor.find_process("actual-worker-job");
    if (!process.has_value() || process->process_id == 0U) {
        return false;
    }
    const auto exits = await_exit(supervisor);
    if (exits.size() != 1U || exits.front().job_id != "actual-worker-job" ||
        exits.front().process_id != process->process_id || exits.front().exit_code != 0 ||
        !exits.front().diagnostics.empty() || !exits.front().protocol_issues.empty() ||
        exits.front().events.size() != 5U) {
        return false;
    }
    const auto& events = exits.front().events;
    return events[0].type == biocore::application::WorkerLifecycleEventType::ready &&
           events[1].type == biocore::application::WorkerLifecycleEventType::heartbeat &&
           events[2].type == biocore::application::WorkerLifecycleEventType::progress &&
           events[2].progress == std::optional<double>{0.5} &&
           events[3].type == biocore::application::WorkerLifecycleEventType::log &&
           events[4].type == biocore::application::WorkerLifecycleEventType::completed &&
           events[4].exit_code == std::optional<std::int64_t>{0};
}

[[nodiscard]] bool test_protocol_rejection_and_stderr_diagnostics(
    const fs::path& probe_executable
) {
    TemporaryDirectory project{"protocol rejection"};
    PlatformWorkerSupervisor supervisor{probe_executable, project.path()};
    supervisor.launch(request("probe-malformed", 9));

    bool saw_issue = false;
    bool saw_diagnostic = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        for (const WorkerProcessOutput& output : supervisor.poll_output()) {
            saw_issue = saw_issue || !output.protocol_issues.empty();
            saw_diagnostic = saw_diagnostic ||
                             (!output.diagnostics.empty() &&
                              output.diagnostics.front() == "probe diagnostic line");
        }
        if (saw_issue && saw_diagnostic) break;
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }

    const auto exits = await_exit(supervisor);
    if (exits.size() != 1U || exits.front().exit_code != 0) return false;
    saw_issue = saw_issue || !exits.front().protocol_issues.empty();
    saw_diagnostic = saw_diagnostic ||
                     (!exits.front().diagnostics.empty() &&
                      exits.front().diagnostics.front() == "probe diagnostic line");
    return saw_issue && saw_diagnostic && exits.front().events.empty();
}

[[nodiscard]] bool test_oversized_protocol_line_is_bounded(
    const fs::path& probe_executable
) {
    TemporaryDirectory project{"oversized protocol"};
    PlatformWorkerSupervisor supervisor{probe_executable, project.path()};
    supervisor.launch(request("probe-oversized", 10));

    std::size_t issue_count = 0U;
    for (int attempt = 0; attempt < 100; ++attempt) {
        for (const WorkerProcessOutput& output : supervisor.poll_output()) {
            issue_count += output.protocol_issues.size();
        }
        if (issue_count != 0U) break;
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    const auto exits = await_exit(supervisor);
    if (exits.size() != 1U || exits.front().exit_code != 0) return false;
    issue_count += exits.front().protocol_issues.size();
    return issue_count == 1U && exits.front().events.empty();
}

[[nodiscard]] bool test_bounded_noisy_worker_drain(const fs::path& probe_executable) {
    TemporaryDirectory project{"bounded noisy drain"};
    PlatformWorkerSupervisor supervisor{probe_executable, project.path()};
    supervisor.launch(request("probe-flood", 77));

    std::size_t total_diagnostics = 0U;
    std::size_t maximum_batch = 0U;
    std::size_t nonempty_batches = 0U;
    std::vector<WorkerProcessExit> exits;
    for (int attempt = 0; attempt < 400 && exits.empty(); ++attempt) {
        for (const WorkerProcessOutput& output : supervisor.poll_output()) {
            if (!output.diagnostics.empty()) {
                ++nonempty_batches;
                maximum_batch = std::max(maximum_batch, output.diagnostics.size());
                total_diagnostics += output.diagnostics.size();
            }
        }
        exits = supervisor.reap_exited();
        if (exits.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
    }
    if (exits.size() != 1U || exits.front().exit_code != 0) return false;
    total_diagnostics += exits.front().diagnostics.size();
    maximum_batch = std::max(maximum_batch, exits.front().diagnostics.size());
    if (!exits.front().diagnostics.empty()) ++nonempty_batches;

    // Each line is 1024 bytes including newline. The supervisor's stderr drain budget is
    // 256 KiB, so one poll/reap drain must not materialize the full 2 MiB flood.
    return total_diagnostics == 2048U && nonempty_batches >= 4U &&
           maximum_batch <= 512U && supervisor.tracked_processes().empty();
}

[[nodiscard]] bool test_configuration_validation(const fs::path& probe_executable) {
    TemporaryDirectory project{"configuration"};

    bool relative_executable_rejected = false;
    try {
        PlatformWorkerSupervisor supervisor{probe_executable.filename(), project.path()};
    } catch (const WorkerSupervisorError& error) {
        relative_executable_rejected =
            error.code() == WorkerSupervisorErrorCode::invalid_configuration;
    }

    bool relative_project_rejected = false;
    try {
        PlatformWorkerSupervisor supervisor{probe_executable, fs::path{"relative-project"}};
    } catch (const WorkerSupervisorError& error) {
        relative_project_rejected =
            error.code() == WorkerSupervisorErrorCode::invalid_configuration;
    }

    const fs::path symlink_root = project.path().parent_path() /
                                  (project.path().filename().string() + "-symlink");
    std::error_code symlink_error;
    fs::create_directory_symlink(project.path(), symlink_root, symlink_error);
    bool symlink_rejected = true;
    if (!symlink_error) {
        try {
            PlatformWorkerSupervisor supervisor{probe_executable, symlink_root};
            symlink_rejected = false;
        } catch (const WorkerSupervisorError& error) {
            symlink_rejected = error.code() == WorkerSupervisorErrorCode::invalid_configuration;
        }
        fs::remove(symlink_root, symlink_error);
    }

    bool directory_as_executable_rejected = false;
    try {
        PlatformWorkerSupervisor supervisor{project.path(), project.path()};
    } catch (const WorkerSupervisorError& error) {
        directory_as_executable_rejected =
            error.code() == WorkerSupervisorErrorCode::invalid_configuration;
    }

    const fs::path regular_file = project.path() / "not-a-directory.txt";
    std::ofstream{regular_file} << "data";
    bool file_as_project_rejected = false;
    try {
        PlatformWorkerSupervisor supervisor{probe_executable, regular_file};
    } catch (const WorkerSupervisorError& error) {
        file_as_project_rejected =
            error.code() == WorkerSupervisorErrorCode::invalid_configuration;
    }

    const fs::path noncanonical_root = project.path() / ".";
    bool noncanonical_rejected = false;
    try {
        PlatformWorkerSupervisor supervisor{probe_executable, noncanonical_root};
    } catch (const WorkerSupervisorError& error) {
        noncanonical_rejected =
            error.code() == WorkerSupervisorErrorCode::invalid_configuration;
    }

#if !defined(_WIN32)
    const fs::path non_executable = project.path() / "non-executable";
    fs::copy_file(probe_executable, non_executable);
    fs::permissions(
        non_executable,
        fs::perms::owner_read | fs::perms::owner_write,
        fs::perm_options::replace
    );
    bool non_executable_rejected = false;
    try {
        PlatformWorkerSupervisor supervisor{fs::canonical(non_executable), project.path()};
    } catch (const WorkerSupervisorError& error) {
        non_executable_rejected =
            error.code() == WorkerSupervisorErrorCode::invalid_configuration;
    }
#else
    const bool non_executable_rejected = true;
#endif

    return relative_executable_rejected && relative_project_rejected && symlink_rejected &&
           directory_as_executable_rejected && file_as_project_rejected &&
           noncanonical_rejected && non_executable_rejected;
}

[[nodiscard]] bool test_request_and_spawn_failures(const fs::path& probe_executable) {
    TemporaryDirectory project{"failures"};
    PlatformWorkerSupervisor supervisor{probe_executable, project.path()};

    bool blank_rejected = false;
    try {
        supervisor.launch(request("   ", 0));
    } catch (const WorkerSupervisorError& error) {
        blank_rejected = error.code() == WorkerSupervisorErrorCode::invalid_launch_request;
    }

    bool negative_revision_rejected = false;
    try {
        supervisor.launch(request("job-negative", -1));
    } catch (const WorkerSupervisorError& error) {
        negative_revision_rejected =
            error.code() == WorkerSupervisorErrorCode::invalid_launch_request;
    }

    const fs::path copied_probe = project.path() / "temporary-probe";
    fs::copy_file(probe_executable, copied_probe);
#if !defined(_WIN32)
    fs::permissions(
        copied_probe,
        fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write,
        fs::perm_options::add
    );
#endif
    const fs::path canonical_probe = fs::canonical(copied_probe);
    PlatformWorkerSupervisor removed_executable_supervisor{canonical_probe, project.path()};
    fs::remove(canonical_probe);

    bool spawn_failure_classified = false;
    try {
        removed_executable_supervisor.launch(request("job-missing-executable", 1));
    } catch (const WorkerSupervisorError& error) {
        spawn_failure_classified = error.code() == WorkerSupervisorErrorCode::spawn_failed &&
                                   error.job_id() == "job-missing-executable";
    }

    return blank_rejected && negative_revision_rejected && spawn_failure_classified &&
           removed_executable_supervisor.tracked_processes().empty();
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    if (argc != 3) {
        std::cerr << "Expected worker process probe and real worker paths\n";
        return EXIT_FAILURE;
    }

    const fs::path probe = fs::canonical(path_from_utf8(argv[1]));
    const fs::path worker = fs::canonical(path_from_utf8(argv[2]));
    if (!test_launch_and_reap(probe)) {
        std::cerr << "PlatformWorkerSupervisor launch/reap contract failed\n";
        return EXIT_FAILURE;
    }
    if (!test_nonzero_exit_and_concurrent_duplicate(probe)) {
        std::cerr << "PlatformWorkerSupervisor concurrent/exit-code contract failed\n";
        return EXIT_FAILURE;
    }
    if (!test_destructor_reaps_completed_child(probe)) {
        std::cerr << "PlatformWorkerSupervisor destructor reap contract failed\n";
        return EXIT_FAILURE;
    }
    if (!test_termination_does_not_overwrite_existing_exit(probe)) {
        std::cerr << "PlatformWorkerSupervisor already-exited termination contract failed\n";
        return EXIT_FAILURE;
    }
    if (!test_force_termination(probe)) {
        std::cerr << "PlatformWorkerSupervisor termination contract failed\n";
        return EXIT_FAILURE;
    }
    if (!test_force_termination_kills_process_tree(probe)) {
        std::cerr << "PlatformWorkerSupervisor process-tree termination contract failed\n";
        return EXIT_FAILURE;
    }
    if (!test_actual_worker_launch(worker)) {
        std::cerr << "PlatformWorkerSupervisor real worker contract failed\n";
        return EXIT_FAILURE;
    }
    if (!test_protocol_rejection_and_stderr_diagnostics(probe)) {
        std::cerr << "PlatformWorkerSupervisor protocol/diagnostic contract failed\n";
        return EXIT_FAILURE;
    }
    if (!test_oversized_protocol_line_is_bounded(probe)) {
        std::cerr << "PlatformWorkerSupervisor oversized protocol contract failed\n";
        return EXIT_FAILURE;
    }
    if (!test_bounded_noisy_worker_drain(probe)) {
        std::cerr << "PlatformWorkerSupervisor bounded noisy-drain contract failed\n";
        return EXIT_FAILURE;
    }
    if (!test_configuration_validation(probe)) {
        std::cerr << "PlatformWorkerSupervisor configuration contract failed\n";
        return EXIT_FAILURE;
    }
    if (!test_request_and_spawn_failures(probe)) {
        std::cerr << "PlatformWorkerSupervisor failure classification contract failed\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
