#include "biocore/infrastructure/platform_worker_supervisor.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstddef>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#include "biocore/infrastructure/windows_process_arguments.hpp"
#include "biocore/infrastructure/worker_event_mapper.hpp"
#include "biocore/worker_protocol/launch_arguments.hpp"
#include "biocore/worker_protocol/ndjson_framer.hpp"
#include "biocore/worker_protocol/worker_event.hpp"
#include "biocore/worker_protocol/worker_event_codec.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace biocore::infrastructure {
namespace {

constexpr std::size_t maximum_diagnostic_line_bytes = 64U * 1024U;
constexpr std::size_t pipe_read_buffer_bytes = 16U * 1024U;
constexpr std::size_t maximum_stdout_bytes_per_drain = 1024U * 1024U;
constexpr std::size_t maximum_stderr_bytes_per_drain = 256U * 1024U;

[[nodiscard]] bool is_blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
               return std::isspace(static_cast<unsigned char>(character)) != 0;
           });
}

void validate_launch_request(const application::WorkerLaunchRequest& request) {
    if (is_blank(request.job_id) || request.job_id.find('\0') != std::string::npos ||
        request.job_id.size() > 128U) {
        throw WorkerSupervisorError{
            WorkerSupervisorErrorCode::invalid_launch_request,
            request.job_id,
            "Worker job id is invalid",
        };
    }
    if (request.job_revision < 0) {
        throw WorkerSupervisorError{
            WorkerSupervisorErrorCode::invalid_launch_request,
            request.job_id,
            "Worker job revision must not be negative",
        };
    }
}

[[nodiscard]] std::filesystem::path validate_existing_canonical_path(
    const std::filesystem::path& input,
    const bool require_regular_file,
    const std::string_view description
) {
    if (input.empty() || !input.is_absolute()) {
        throw WorkerSupervisorError{
            WorkerSupervisorErrorCode::invalid_configuration,
            {},
            std::string{description} + " must be an absolute path",
        };
    }

    std::error_code error;
    const auto status = std::filesystem::symlink_status(input, error);
    if (error) {
        throw WorkerSupervisorError{
            WorkerSupervisorErrorCode::invalid_configuration,
            {},
            std::string{description} + " could not be inspected: " + error.message(),
        };
    }
    if (std::filesystem::is_symlink(status)) {
        throw WorkerSupervisorError{
            WorkerSupervisorErrorCode::invalid_configuration,
            {},
            std::string{description} + " must not be a symbolic link",
        };
    }

    const auto canonical_path = std::filesystem::canonical(input, error);
    if (error) {
        throw WorkerSupervisorError{
            WorkerSupervisorErrorCode::invalid_configuration,
            {},
            std::string{description} + " must exist: " + error.message(),
        };
    }
    if (input.lexically_normal() != canonical_path) {
        throw WorkerSupervisorError{
            WorkerSupervisorErrorCode::invalid_configuration,
            {},
            std::string{description} + " must already be canonical",
        };
    }

    const bool expected_type = require_regular_file
                                   ? std::filesystem::is_regular_file(canonical_path, error)
                                   : std::filesystem::is_directory(canonical_path, error);
    if (error || !expected_type) {
        throw WorkerSupervisorError{
            WorkerSupervisorErrorCode::invalid_configuration,
            {},
            std::string{description} +
                (require_regular_file ? " must be a regular file" : " must be a directory"),
        };
    }

#if !defined(_WIN32)
    if (require_regular_file && ::access(canonical_path.c_str(), X_OK) != 0) {
        throw WorkerSupervisorError{
            WorkerSupervisorErrorCode::invalid_configuration,
            {},
            std::string{description} + " is not executable",
        };
    }
#endif

    return canonical_path;
}

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string value = path.u8string();
    return std::string{reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] std::filesystem::path path_from_utf8(const std::string_view value) {
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const char character : value) {
        utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
    }
    return std::filesystem::path{utf8};
}

[[nodiscard]] bool is_within_project_root(
    const std::filesystem::path& project_root,
    const std::filesystem::path& candidate
) {
    const std::filesystem::path relative = candidate.lexically_relative(project_root);
    if (relative.empty() || relative.is_absolute()) return false;
    const auto first = relative.begin();
    return first != relative.end() && *first != "..";
}

[[nodiscard]] std::optional<std::filesystem::path> validate_execution_plan_path(
    const application::WorkerLaunchRequest& request,
    const std::filesystem::path& project_root
) {
    if (!request.execution_plan_path.has_value()) return std::nullopt;
    const std::string& value = *request.execution_plan_path;
    if (is_blank(value) || value.find('\0') != std::string::npos || value.size() > 32U * 1024U) {
        throw WorkerSupervisorError{
            WorkerSupervisorErrorCode::invalid_launch_request,
            request.job_id,
            "Worker execution-plan path is invalid",
        };
    }
    const std::filesystem::path input = path_from_utf8(value);
    if (!input.is_absolute()) {
        throw WorkerSupervisorError{
            WorkerSupervisorErrorCode::invalid_launch_request,
            request.job_id,
            "Worker execution-plan path must be absolute",
        };
    }
    std::error_code error;
    const auto status = std::filesystem::symlink_status(input, error);
    if (error || std::filesystem::is_symlink(status)) {
        throw WorkerSupervisorError{
            WorkerSupervisorErrorCode::invalid_launch_request,
            request.job_id,
            "Worker execution-plan path must not be a symbolic link",
        };
    }
    const std::filesystem::path canonical = std::filesystem::canonical(input, error);
    if (error || input.lexically_normal() != canonical ||
        !std::filesystem::is_regular_file(canonical, error) || error ||
        !is_within_project_root(project_root, canonical)) {
        throw WorkerSupervisorError{
            WorkerSupervisorErrorCode::invalid_launch_request,
            request.job_id,
            "Worker execution-plan path must be a canonical project-local regular file",
        };
    }
    return canonical;
}

[[nodiscard]] std::vector<std::string> make_arguments(
    const std::filesystem::path& executable,
    const std::filesystem::path& project_root,
    const application::WorkerLaunchRequest& request
) {
    std::vector<std::string> arguments{
        path_to_utf8(executable),
        std::string{worker_protocol::job_id_argument},
        request.job_id,
        std::string{worker_protocol::project_root_argument},
        path_to_utf8(project_root),
        std::string{worker_protocol::job_revision_argument},
        std::to_string(request.job_revision),
    };
    const auto execution_plan = validate_execution_plan_path(request, project_root);
    if (execution_plan.has_value()) {
        arguments.push_back(std::string{worker_protocol::execution_plan_argument});
        arguments.push_back(path_to_utf8(*execution_plan));
    }
    return arguments;
}

#if defined(_WIN32)
[[nodiscard]] std::wstring utf8_to_wide(const std::string_view value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("UTF-8 argument is too long");
    }
    const int value_size = static_cast<int>(value.size());
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), value_size, nullptr, 0
    );
    if (required <= 0) {
        throw std::invalid_argument("Worker argument is not valid UTF-8");
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), value_size, result.data(), required
        ) != required) {
        throw std::invalid_argument("Worker argument could not be converted to UTF-16");
    }
    return result;
}

class WindowsHandle final {
public:
    WindowsHandle() = default;
    explicit WindowsHandle(HANDLE handle) noexcept : handle_{handle} {}
    ~WindowsHandle() {
        if (valid()) CloseHandle(handle_);
    }
    WindowsHandle(const WindowsHandle&) = delete;
    WindowsHandle& operator=(const WindowsHandle&) = delete;
    WindowsHandle(WindowsHandle&& other) noexcept
        : handle_{std::exchange(other.handle_, nullptr)} {}
    WindowsHandle& operator=(WindowsHandle&& other) noexcept {
        if (this != &other) {
            if (valid()) CloseHandle(handle_);
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] HANDLE release() noexcept {
        return std::exchange(handle_, nullptr);
    }
    [[nodiscard]] bool valid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }
private:
    HANDLE handle_{nullptr};
};

struct WindowsPipe final {
    HANDLE read_handle{nullptr};
    HANDLE write_handle{nullptr};

    ~WindowsPipe() {
        if (read_handle != nullptr) CloseHandle(read_handle);
        if (write_handle != nullptr) CloseHandle(write_handle);
    }

    WindowsPipe(const WindowsPipe&) = delete;
    WindowsPipe& operator=(const WindowsPipe&) = delete;
    WindowsPipe() = default;
    WindowsPipe(WindowsPipe&& other) noexcept
        : read_handle{std::exchange(other.read_handle, nullptr)},
          write_handle{std::exchange(other.write_handle, nullptr)} {}
    WindowsPipe& operator=(WindowsPipe&& other) noexcept {
        if (this != &other) {
            if (read_handle != nullptr) CloseHandle(read_handle);
            if (write_handle != nullptr) CloseHandle(write_handle);
            read_handle = std::exchange(other.read_handle, nullptr);
            write_handle = std::exchange(other.write_handle, nullptr);
        }
        return *this;
    }

    [[nodiscard]] HANDLE release_read() noexcept {
        HANDLE result = read_handle;
        read_handle = nullptr;
        return result;
    }
};

[[nodiscard]] WindowsPipe create_windows_pipe(const std::string& job_id) {
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;

    WindowsPipe pipe;
    if (CreatePipe(&pipe.read_handle, &pipe.write_handle, &attributes, 0U) == FALSE) {
        throw WorkerSupervisorError{
            WorkerSupervisorErrorCode::spawn_failed,
            job_id,
            "CreatePipe failed with error " + std::to_string(GetLastError()),
        };
    }
    if (SetHandleInformation(pipe.read_handle, HANDLE_FLAG_INHERIT, 0U) == FALSE) {
        throw WorkerSupervisorError{
            WorkerSupervisorErrorCode::spawn_failed,
            job_id,
            "SetHandleInformation failed with error " + std::to_string(GetLastError()),
        };
    }
    return pipe;
}
#else
struct PosixPipe final {
    int read_fd{-1};
    int write_fd{-1};

    ~PosixPipe() {
        if (read_fd >= 0) ::close(read_fd);
        if (write_fd >= 0) ::close(write_fd);
    }

    PosixPipe(const PosixPipe&) = delete;
    PosixPipe& operator=(const PosixPipe&) = delete;
    PosixPipe() = default;
    PosixPipe(PosixPipe&& other) noexcept
        : read_fd{std::exchange(other.read_fd, -1)},
          write_fd{std::exchange(other.write_fd, -1)} {}
    PosixPipe& operator=(PosixPipe&& other) noexcept {
        if (this != &other) {
            if (read_fd >= 0) ::close(read_fd);
            if (write_fd >= 0) ::close(write_fd);
            read_fd = std::exchange(other.read_fd, -1);
            write_fd = std::exchange(other.write_fd, -1);
        }
        return *this;
    }

    [[nodiscard]] int release_read() noexcept {
        const int result = read_fd;
        read_fd = -1;
        return result;
    }
};

void configure_fd(const int fd, const int command, const int value, const std::string& job_id) {
    if (::fcntl(fd, command, value) == -1) {
        throw WorkerSupervisorError{
            WorkerSupervisorErrorCode::spawn_failed,
            job_id,
            "fcntl failed: " + std::error_code{errno, std::generic_category()}.message(),
        };
    }
}

[[nodiscard]] PosixPipe create_posix_pipe(const std::string& job_id) {
    std::array<int, 2U> descriptors{-1, -1};
    if (::pipe(descriptors.data()) != 0) {
        throw WorkerSupervisorError{
            WorkerSupervisorErrorCode::spawn_failed,
            job_id,
            "pipe failed: " + std::error_code{errno, std::generic_category()}.message(),
        };
    }

    PosixPipe pipe;
    pipe.read_fd = descriptors[0];
    pipe.write_fd = descriptors[1];
    const int read_flags = ::fcntl(pipe.read_fd, F_GETFL, 0);
    if (read_flags == -1) {
        throw WorkerSupervisorError{
            WorkerSupervisorErrorCode::spawn_failed,
            job_id,
            "fcntl(F_GETFL) failed: " +
                std::error_code{errno, std::generic_category()}.message(),
        };
    }
    configure_fd(pipe.read_fd, F_SETFL, read_flags | O_NONBLOCK, job_id);
    configure_fd(pipe.read_fd, F_SETFD, FD_CLOEXEC, job_id);
    configure_fd(pipe.write_fd, F_SETFD, FD_CLOEXEC, job_id);
    return pipe;
}
#endif

}  // namespace

WorkerSupervisorError::WorkerSupervisorError(
    const WorkerSupervisorErrorCode code,
    std::string job_id,
    std::string message
)
    : std::runtime_error{std::move(message)}, code_{code}, job_id_{std::move(job_id)} {}

WorkerSupervisorErrorCode WorkerSupervisorError::code() const noexcept { return code_; }
std::string_view WorkerSupervisorError::job_id() const noexcept { return job_id_; }

class PlatformWorkerSupervisor::Impl final {
public:
    Impl(std::filesystem::path worker_executable, std::filesystem::path project_root)
        : worker_executable_{validate_existing_canonical_path(
              worker_executable, true, "Worker executable"
          )},
          project_root_{validate_existing_canonical_path(project_root, false, "Project root")} {}

    ~Impl() {
        try {
            static_cast<void>(reap_exited());
        } catch (...) {
        }
        std::scoped_lock lock{mutex_};
        for (auto& [job_id, process] : processes_) {
            static_cast<void>(job_id);
            close_streams(process);
#if defined(_WIN32)
            if (process.job_handle != nullptr) CloseHandle(process.job_handle);
            if (process.handle != nullptr) CloseHandle(process.handle);
#endif
        }
    }

    void launch(const application::WorkerLaunchRequest& request) {
        validate_launch_request(request);
        const auto arguments = make_arguments(worker_executable_, project_root_, request);

        std::scoped_lock lock{mutex_};
        const auto [process_iterator, inserted] = processes_.try_emplace(
            request.job_id, NativeProcess{}
        );
        if (!inserted) {
            throw WorkerSupervisorError{
                WorkerSupervisorErrorCode::duplicate_active_job,
                request.job_id,
                "A worker process is already tracked for this job",
            };
        }

        try {
#if defined(_WIN32)
            launch_windows(process_iterator->second, request.job_id, arguments);
#else
            launch_posix(process_iterator->second, request.job_id, arguments);
#endif
        } catch (...) {
            processes_.erase(process_iterator);
            throw;
        }
    }

    [[nodiscard]] std::optional<WorkerProcessInfo> find_process(
        const std::string_view job_id
    ) const {
        std::scoped_lock lock{mutex_};
        const auto iterator = processes_.find(std::string{job_id});
        if (iterator == processes_.end()) return std::nullopt;
        return WorkerProcessInfo{iterator->first, iterator->second.process_id};
    }

    [[nodiscard]] std::vector<WorkerProcessInfo> tracked_processes() const {
        std::scoped_lock lock{mutex_};
        std::vector<WorkerProcessInfo> result;
        result.reserve(processes_.size());
        for (const auto& [job_id, process] : processes_) {
            result.push_back(WorkerProcessInfo{job_id, process.process_id});
        }
        return result;
    }

    [[nodiscard]] std::vector<WorkerProcessOutput> poll_output() {
        std::scoped_lock lock{mutex_};
        std::vector<WorkerProcessOutput> result;
        for (auto& [job_id, process] : processes_) {
            drain_process(job_id, process);
            if (!process.events.empty() || !process.diagnostics.empty() ||
                !process.protocol_issues.empty()) {
                result.push_back(take_output(job_id, process));
            }
        }
        return result;
    }

    [[nodiscard]] WorkerTerminationResult terminate(const std::string_view job_id) {
        std::scoped_lock lock{mutex_};
        const auto iterator = processes_.find(std::string{job_id});
        if (iterator == processes_.end()) {
            return WorkerTerminationResult::not_found;
        }
        if (query_exit(iterator->first, iterator->second).has_value()) {
            return WorkerTerminationResult::already_exited;
        }
#if defined(_WIN32)
        constexpr UINT cancellation_exit_code = 137U;
        if (iterator->second.job_handle == nullptr ||
            TerminateJobObject(iterator->second.job_handle, cancellation_exit_code) == FALSE) {
            throw WorkerSupervisorError{
                WorkerSupervisorErrorCode::termination_failed,
                iterator->first,
                "TerminateJobObject failed with error " + std::to_string(GetLastError()),
            };
        }
#else
        const auto process_group_id = static_cast<pid_t>(iterator->second.process_group_id);
        if (process_group_id <= 0 || ::killpg(process_group_id, SIGKILL) != 0) {
            if (errno == ESRCH) {
                return WorkerTerminationResult::not_found;
            }
            throw WorkerSupervisorError{
                WorkerSupervisorErrorCode::termination_failed,
                iterator->first,
                "killpg(SIGKILL) failed: " +
                    std::error_code{errno, std::generic_category()}.message(),
            };
        }
#endif
        return WorkerTerminationResult::requested;
    }

    [[nodiscard]] std::vector<WorkerProcessExit> reap_exited() {
        std::scoped_lock lock{mutex_};
        std::vector<WorkerProcessExit> result;
        for (auto iterator = processes_.begin(); iterator != processes_.end();) {
            drain_process(iterator->first, iterator->second);
            const auto exit_code = query_exit(iterator->first, iterator->second);
            if (!exit_code.has_value()) {
                ++iterator;
                continue;
            }

            drain_process(iterator->first, iterator->second);
            if (streams_open(iterator->second)) {
                ++iterator;
                continue;
            }
            WorkerProcessExit exit{
                .job_id = iterator->first,
                .process_id = iterator->second.process_id,
                .exit_code = *exit_code,
                .events = std::move(iterator->second.events),
                .diagnostics = std::move(iterator->second.diagnostics),
                .protocol_issues = std::move(iterator->second.protocol_issues),
            };
#if defined(_WIN32)
            if (iterator->second.job_handle != nullptr) {
                CloseHandle(iterator->second.job_handle);
                iterator->second.job_handle = nullptr;
            }
            CloseHandle(iterator->second.handle);
            iterator->second.handle = nullptr;
#endif
            result.push_back(std::move(exit));
            iterator = processes_.erase(iterator);
        }
        return result;
    }

    [[nodiscard]] const std::filesystem::path& worker_executable() const noexcept {
        return worker_executable_;
    }
    [[nodiscard]] const std::filesystem::path& project_root() const noexcept {
        return project_root_;
    }

private:
    struct NativeProcess final {
        std::uint64_t process_id{0U};
#if defined(_WIN32)
        HANDLE handle{nullptr};
        HANDLE job_handle{nullptr};
        HANDLE stdout_read{nullptr};
        HANDLE stderr_read{nullptr};
#else
        std::uint64_t process_group_id{0U};
        int stdout_fd{-1};
        int stderr_fd{-1};
#endif
        worker_protocol::NdjsonFramer stdout_framer{worker_protocol::maximum_event_line_bytes};
        worker_protocol::NdjsonFramer stderr_framer{maximum_diagnostic_line_bytes};
        std::vector<application::WorkerLifecycleEvent> events;
        std::vector<std::string> diagnostics;
        std::vector<WorkerProtocolIssue> protocol_issues;
        std::optional<std::int64_t> cached_exit_code;
        bool stdout_protocol_failed{false};
        bool stderr_framing_failed{false};
    };

#if defined(_WIN32)
    void launch_windows(
        NativeProcess& process,
        const std::string& job_id,
        const std::vector<std::string>& arguments
    ) {
        WindowsPipe stdout_pipe = create_windows_pipe(job_id);
        WindowsPipe stderr_pipe = create_windows_pipe(job_id);

        std::vector<std::wstring> wide_arguments;
        try {
            wide_arguments.reserve(arguments.size());
            for (const std::string& argument : arguments) {
                wide_arguments.push_back(utf8_to_wide(argument));
            }
        } catch (const std::invalid_argument& error) {
            throw WorkerSupervisorError{
                WorkerSupervisorErrorCode::invalid_launch_request, job_id, error.what()
            };
        }
        std::wstring command_line = make_windows_process_command_line(wide_arguments);
        SECURITY_ATTRIBUTES input_attributes{};
        input_attributes.nLength = sizeof(input_attributes);
        input_attributes.bInheritHandle = TRUE;
        WindowsHandle null_input{CreateFileW(
            L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            &input_attributes, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr
        )};
        if (!null_input.valid()) {
            throw WorkerSupervisorError{
                WorkerSupervisorErrorCode::spawn_failed,
                job_id,
                "Unable to open NUL for worker stdin: " + std::to_string(GetLastError()),
            };
        }

        SIZE_T attribute_list_size = 0U;
        static_cast<void>(InitializeProcThreadAttributeList(
            nullptr, 1U, 0U, &attribute_list_size
        ));
        if (attribute_list_size == 0U) {
            throw WorkerSupervisorError{
                WorkerSupervisorErrorCode::spawn_failed,
                job_id,
                "Unable to size the worker handle inheritance list",
            };
        }
        std::vector<std::byte> attribute_storage(attribute_list_size);
        auto* attribute_list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
            attribute_storage.data()
        );
        if (InitializeProcThreadAttributeList(
                attribute_list, 1U, 0U, &attribute_list_size
            ) == FALSE) {
            throw WorkerSupervisorError{
                WorkerSupervisorErrorCode::spawn_failed,
                job_id,
                "InitializeProcThreadAttributeList failed with error " +
                    std::to_string(GetLastError()),
            };
        }
        struct AttributeListGuard final {
            LPPROC_THREAD_ATTRIBUTE_LIST list;
            ~AttributeListGuard() { DeleteProcThreadAttributeList(list); }
        } attribute_guard{attribute_list};

        std::array<HANDLE, 3U> inherited_handles{
            stdout_pipe.write_handle, stderr_pipe.write_handle, null_input.get()
        };
        if (UpdateProcThreadAttribute(
                attribute_list, 0U, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                inherited_handles.data(), sizeof(inherited_handles), nullptr, nullptr
            ) == FALSE) {
            throw WorkerSupervisorError{
                WorkerSupervisorErrorCode::spawn_failed,
                job_id,
                "UpdateProcThreadAttribute failed with error " +
                    std::to_string(GetLastError()),
            };
        }

        STARTUPINFOEXW startup_info{};
        startup_info.StartupInfo.cb = sizeof(startup_info);
        startup_info.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup_info.StartupInfo.hStdInput = null_input.get();
        startup_info.StartupInfo.hStdOutput = stdout_pipe.write_handle;
        startup_info.StartupInfo.hStdError = stderr_pipe.write_handle;
        startup_info.lpAttributeList = attribute_list;
        WindowsHandle job_handle{CreateJobObjectW(nullptr, nullptr)};
        if (!job_handle.valid()) {
            throw WorkerSupervisorError{
                WorkerSupervisorErrorCode::spawn_failed,
                job_id,
                "CreateJobObjectW failed with error " + std::to_string(GetLastError()),
            };
        }
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_limits{};
        job_limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (SetInformationJobObject(
                job_handle.get(), JobObjectExtendedLimitInformation, &job_limits,
                sizeof(job_limits)
            ) == FALSE) {
            throw WorkerSupervisorError{
                WorkerSupervisorErrorCode::spawn_failed,
                job_id,
                "SetInformationJobObject failed with error " +
                    std::to_string(GetLastError()),
            };
        }

        PROCESS_INFORMATION process_information{};
        const std::wstring executable = worker_executable_.native();

        const BOOL created = CreateProcessW(
            executable.c_str(), command_line.data(), nullptr, nullptr, TRUE,
            CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED,
            nullptr, nullptr, &startup_info.StartupInfo, &process_information
        );
        if (created == FALSE) {
            throw WorkerSupervisorError{
                WorkerSupervisorErrorCode::spawn_failed,
                job_id,
                "CreateProcessW failed with error " + std::to_string(GetLastError()),
            };
        }
        WindowsHandle process_handle{process_information.hProcess};
        WindowsHandle thread_handle{process_information.hThread};

        if (AssignProcessToJobObject(job_handle.get(), process_handle.get()) == FALSE) {
            const DWORD error = GetLastError();
            static_cast<void>(TerminateProcess(process_handle.get(), 137U));
            throw WorkerSupervisorError{
                WorkerSupervisorErrorCode::spawn_failed,
                job_id,
                "AssignProcessToJobObject failed with error " + std::to_string(error),
            };
        }
        if (ResumeThread(thread_handle.get()) == static_cast<DWORD>(-1)) {
            const DWORD error = GetLastError();
            throw WorkerSupervisorError{
                WorkerSupervisorErrorCode::spawn_failed,
                job_id,
                "ResumeThread failed with error " + std::to_string(error),
            };
        }

        CloseHandle(stdout_pipe.write_handle);
        stdout_pipe.write_handle = nullptr;
        CloseHandle(stderr_pipe.write_handle);
        stderr_pipe.write_handle = nullptr;

        process.process_id = static_cast<std::uint64_t>(process_information.dwProcessId);
        process.handle = process_handle.release();
        process.job_handle = job_handle.release();
        process.stdout_read = stdout_pipe.release_read();
        process.stderr_read = stderr_pipe.release_read();
    }
#else
    void launch_posix(
        NativeProcess& process,
        const std::string& job_id,
        const std::vector<std::string>& arguments
    ) {
        PosixPipe stdout_pipe = create_posix_pipe(job_id);
        PosixPipe stderr_pipe = create_posix_pipe(job_id);

        posix_spawn_file_actions_t actions{};
        const int init_result = ::posix_spawn_file_actions_init(&actions);
        if (init_result != 0) {
            throw WorkerSupervisorError{
                WorkerSupervisorErrorCode::spawn_failed,
                job_id,
                "posix_spawn_file_actions_init failed: " +
                    std::error_code{init_result, std::generic_category()}.message(),
            };
        }
        struct ActionsGuard final {
            posix_spawn_file_actions_t* actions;
            ~ActionsGuard() { ::posix_spawn_file_actions_destroy(actions); }
        } guard{&actions};

        const std::array<int, 6U> action_results{
            ::posix_spawn_file_actions_adddup2(&actions, stdout_pipe.write_fd, STDOUT_FILENO),
            ::posix_spawn_file_actions_adddup2(&actions, stderr_pipe.write_fd, STDERR_FILENO),
            ::posix_spawn_file_actions_addclose(&actions, stdout_pipe.read_fd),
            ::posix_spawn_file_actions_addclose(&actions, stderr_pipe.read_fd),
            ::posix_spawn_file_actions_addclose(&actions, stdout_pipe.write_fd),
            ::posix_spawn_file_actions_addclose(&actions, stderr_pipe.write_fd),
        };
        for (const int action_result : action_results) {
            if (action_result != 0) {
                throw WorkerSupervisorError{
                    WorkerSupervisorErrorCode::spawn_failed,
                    job_id,
                    "posix_spawn file action failed: " +
                        std::error_code{action_result, std::generic_category()}.message(),
                };
            }
        }

        std::vector<char*> native_arguments;
        native_arguments.reserve(arguments.size() + 1U);
        for (const std::string& argument : arguments) {
            native_arguments.push_back(const_cast<char*>(argument.c_str()));
        }
        native_arguments.push_back(nullptr);

        posix_spawnattr_t attributes{};
        const int attribute_init_result = ::posix_spawnattr_init(&attributes);
        if (attribute_init_result != 0) {
            throw WorkerSupervisorError{
                WorkerSupervisorErrorCode::spawn_failed,
                job_id,
                "posix_spawnattr_init failed: " +
                    std::error_code{attribute_init_result, std::generic_category()}.message(),
            };
        }
        struct AttributesGuard final {
            posix_spawnattr_t* attributes;
            ~AttributesGuard() { ::posix_spawnattr_destroy(attributes); }
        } attributes_guard{&attributes};

        int attribute_result = ::posix_spawnattr_setpgroup(&attributes, 0);
        if (attribute_result == 0) {
            attribute_result = ::posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
        }
        if (attribute_result != 0) {
            throw WorkerSupervisorError{
                WorkerSupervisorErrorCode::spawn_failed,
                job_id,
                "Unable to configure worker process group: " +
                    std::error_code{attribute_result, std::generic_category()}.message(),
            };
        }

        pid_t process_id = 0;
        const int spawn_result = ::posix_spawn(
            &process_id, worker_executable_.c_str(), &actions, &attributes,
            native_arguments.data(), environ
        );
        if (spawn_result != 0) {
            throw WorkerSupervisorError{
                WorkerSupervisorErrorCode::spawn_failed,
                job_id,
                "posix_spawn failed: " +
                    std::error_code{spawn_result, std::generic_category()}.message(),
            };
        }
        ::close(stdout_pipe.write_fd);
        stdout_pipe.write_fd = -1;
        ::close(stderr_pipe.write_fd);
        stderr_pipe.write_fd = -1;

        process.process_id = static_cast<std::uint64_t>(process_id);
        process.process_group_id = static_cast<std::uint64_t>(process_id);
        process.stdout_fd = stdout_pipe.release_read();
        process.stderr_fd = stderr_pipe.release_read();
    }
#endif

    void append_stdout_line(
        const std::string& job_id,
        NativeProcess& process,
        const std::string_view line
    ) {
        try {
            process.events.push_back(to_application_event(worker_protocol::parse_worker_event(line)));
        } catch (const std::exception& error) {
            process.protocol_issues.push_back(WorkerProtocolIssue{job_id, error.what()});
        }
    }

    void finish_stdout(const std::string& job_id, NativeProcess& process) {
        if (process.stdout_protocol_failed) return;
        const auto final_line = process.stdout_framer.finish();
        if (final_line.has_value()) append_stdout_line(job_id, process, *final_line);
    }

    void finish_stderr(NativeProcess& process) {
        if (process.stderr_framing_failed) return;
        const auto final_line = process.stderr_framer.finish();
        if (final_line.has_value() && !final_line->empty()) {
            process.diagnostics.push_back(*final_line);
        }
    }

    void append_stdout_bytes(
        const std::string& job_id,
        NativeProcess& process,
        const std::string_view bytes
    ) {
        if (process.stdout_protocol_failed) return;
        try {
            for (const std::string& line : process.stdout_framer.feed(bytes)) {
                append_stdout_line(job_id, process, line);
            }
        } catch (const std::exception& error) {
            process.stdout_protocol_failed = true;
            process.protocol_issues.push_back(WorkerProtocolIssue{job_id, error.what()});
        }
    }

    void append_stderr_bytes(NativeProcess& process, const std::string_view bytes) {
        if (process.stderr_framing_failed) return;
        try {
            for (std::string line : process.stderr_framer.feed(bytes)) {
                if (!line.empty()) process.diagnostics.push_back(std::move(line));
            }
        } catch (const std::exception& error) {
            process.stderr_framing_failed = true;
            process.diagnostics.push_back(std::string{"stderr framing error: "} + error.what());
        }
    }

    void drain_process(const std::string& job_id, NativeProcess& process) {
#if defined(_WIN32)
        drain_windows_handle(
            job_id, process, true, maximum_stdout_bytes_per_drain
        );
        drain_windows_handle(
            job_id, process, false, maximum_stderr_bytes_per_drain
        );
#else
        drain_posix_fd(
            job_id, process, true, maximum_stdout_bytes_per_drain
        );
        drain_posix_fd(
            job_id, process, false, maximum_stderr_bytes_per_drain
        );
#endif
    }

#if defined(_WIN32)
    void drain_windows_handle(
        const std::string& job_id,
        NativeProcess& process,
        const bool stdout_stream,
        const std::size_t byte_budget
    ) {
        HANDLE& handle = stdout_stream ? process.stdout_read : process.stderr_read;
        if (handle == nullptr) return;
        std::array<char, pipe_read_buffer_bytes> buffer{};
        std::size_t drained_bytes = 0U;
        for (;;) {
            if (drained_bytes >= byte_budget) return;
            DWORD available = 0U;
            if (PeekNamedPipe(handle, nullptr, 0U, nullptr, &available, nullptr) == FALSE) {
                const DWORD error = GetLastError();
                if (error == ERROR_BROKEN_PIPE) {
                    if (stdout_stream) finish_stdout(job_id, process); else finish_stderr(process);
                    CloseHandle(handle);
                    handle = nullptr;
                    return;
                }
                throw WorkerSupervisorError{
                    WorkerSupervisorErrorCode::process_query_failed,
                    job_id,
                    "PeekNamedPipe failed with error " + std::to_string(error),
                };
            }
            if (available == 0U) return;
            const std::size_t remaining = byte_budget - drained_bytes;
            const DWORD requested = static_cast<DWORD>(
                std::min<std::size_t>({available, buffer.size(), remaining})
            );
            DWORD read_count = 0U;
            if (ReadFile(handle, buffer.data(), requested, &read_count, nullptr) == FALSE) {
                const DWORD error = GetLastError();
                if (error == ERROR_BROKEN_PIPE) continue;
                throw WorkerSupervisorError{
                    WorkerSupervisorErrorCode::process_query_failed,
                    job_id,
                    "ReadFile failed with error " + std::to_string(error),
                };
            }
            drained_bytes += static_cast<std::size_t>(read_count);
            const std::string_view bytes{buffer.data(), static_cast<std::size_t>(read_count)};
            if (stdout_stream) append_stdout_bytes(job_id, process, bytes);
            else append_stderr_bytes(process, bytes);
        }
    }
#else
    void drain_posix_fd(
        const std::string& job_id,
        NativeProcess& process,
        const bool stdout_stream,
        const std::size_t byte_budget
    ) {
        int& fd = stdout_stream ? process.stdout_fd : process.stderr_fd;
        if (fd < 0) return;
        std::array<char, pipe_read_buffer_bytes> buffer{};
        std::size_t drained_bytes = 0U;
        for (;;) {
            if (drained_bytes >= byte_budget) return;
            const std::size_t remaining = byte_budget - drained_bytes;
            const std::size_t requested = std::min(buffer.size(), remaining);
            const ssize_t read_count = ::read(fd, buffer.data(), requested);
            if (read_count > 0) {
                drained_bytes += static_cast<std::size_t>(read_count);
                const std::string_view bytes{
                    buffer.data(), static_cast<std::size_t>(read_count)
                };
                if (stdout_stream) append_stdout_bytes(job_id, process, bytes);
                else append_stderr_bytes(process, bytes);
                continue;
            }
            if (read_count == 0) {
                if (stdout_stream) finish_stdout(job_id, process); else finish_stderr(process);
                ::close(fd);
                fd = -1;
                return;
            }
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            throw WorkerSupervisorError{
                WorkerSupervisorErrorCode::process_query_failed,
                job_id,
                "read failed: " + std::error_code{errno, std::generic_category()}.message(),
            };
        }
    }
#endif

    [[nodiscard]] std::optional<std::int64_t> query_exit(
        const std::string& job_id,
        NativeProcess& process
    ) {
        if (process.cached_exit_code.has_value()) {
            return process.cached_exit_code;
        }
#if defined(_WIN32)
        const DWORD wait_result = WaitForSingleObject(process.handle, 0U);
        if (wait_result == WAIT_TIMEOUT) return std::nullopt;
        if (wait_result != WAIT_OBJECT_0) {
            throw WorkerSupervisorError{
                WorkerSupervisorErrorCode::process_query_failed,
                job_id,
                "WaitForSingleObject failed with error " + std::to_string(GetLastError()),
            };
        }
        DWORD exit_code = 0U;
        if (GetExitCodeProcess(process.handle, &exit_code) == FALSE) {
            throw WorkerSupervisorError{
                WorkerSupervisorErrorCode::process_query_failed,
                job_id,
                "GetExitCodeProcess failed with error " + std::to_string(GetLastError()),
            };
        }
        process.cached_exit_code = static_cast<std::int64_t>(exit_code);
        return process.cached_exit_code;
#else
        int status = 0;
        const pid_t wait_result = ::waitpid(static_cast<pid_t>(process.process_id), &status, WNOHANG);
        if (wait_result == 0) return std::nullopt;
        if (wait_result < 0) {
            if (errno == EINTR) return std::nullopt;
            throw WorkerSupervisorError{
                WorkerSupervisorErrorCode::process_query_failed,
                job_id,
                "waitpid failed: " + std::error_code{errno, std::generic_category()}.message(),
            };
        }
        if (WIFEXITED(status)) {
            process.cached_exit_code = static_cast<std::int64_t>(WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            process.cached_exit_code =
                128 + static_cast<std::int64_t>(WTERMSIG(status));
        } else {
            process.cached_exit_code = 1;
        }
        return process.cached_exit_code;
#endif
    }

    [[nodiscard]] WorkerProcessOutput take_output(
        const std::string& job_id,
        NativeProcess& process
    ) {
        return WorkerProcessOutput{
            .job_id = job_id,
            .process_id = process.process_id,
            .events = std::exchange(process.events, {}),
            .diagnostics = std::exchange(process.diagnostics, {}),
            .protocol_issues = std::exchange(process.protocol_issues, {}),
        };
    }

    [[nodiscard]] static bool streams_open(const NativeProcess& process) noexcept {
#if defined(_WIN32)
        return process.stdout_read != nullptr || process.stderr_read != nullptr;
#else
        return process.stdout_fd >= 0 || process.stderr_fd >= 0;
#endif
    }

    static void close_streams(NativeProcess& process) noexcept {
#if defined(_WIN32)
        if (process.stdout_read != nullptr) {
            CloseHandle(process.stdout_read);
            process.stdout_read = nullptr;
        }
        if (process.stderr_read != nullptr) {
            CloseHandle(process.stderr_read);
            process.stderr_read = nullptr;
        }
#else
        if (process.stdout_fd >= 0) {
            ::close(process.stdout_fd);
            process.stdout_fd = -1;
        }
        if (process.stderr_fd >= 0) {
            ::close(process.stderr_fd);
            process.stderr_fd = -1;
        }
#endif
    }

    std::filesystem::path worker_executable_;
    std::filesystem::path project_root_;
    mutable std::mutex mutex_;
    std::map<std::string, NativeProcess, std::less<>> processes_;
};

PlatformWorkerSupervisor::PlatformWorkerSupervisor(
    std::filesystem::path worker_executable,
    std::filesystem::path project_root
)
    : impl_{std::make_unique<Impl>(std::move(worker_executable), std::move(project_root))} {}

PlatformWorkerSupervisor::~PlatformWorkerSupervisor() = default;
PlatformWorkerSupervisor::PlatformWorkerSupervisor(PlatformWorkerSupervisor&&) noexcept = default;
PlatformWorkerSupervisor& PlatformWorkerSupervisor::operator=(PlatformWorkerSupervisor&&) noexcept = default;

void PlatformWorkerSupervisor::launch(const application::WorkerLaunchRequest& request) {
    impl_->launch(request);
}
std::optional<WorkerProcessInfo> PlatformWorkerSupervisor::find_process(
    const std::string_view job_id
) const { return impl_->find_process(job_id); }
std::vector<WorkerProcessInfo> PlatformWorkerSupervisor::tracked_processes() const {
    return impl_->tracked_processes();
}
std::vector<WorkerProcessOutput> PlatformWorkerSupervisor::poll_output() {
    return impl_->poll_output();
}
std::vector<WorkerProcessExit> PlatformWorkerSupervisor::reap_exited() {
    return impl_->reap_exited();
}
WorkerTerminationResult PlatformWorkerSupervisor::terminate(
    const std::string_view job_id
) {
    return impl_->terminate(job_id);
}
const std::filesystem::path& PlatformWorkerSupervisor::worker_executable() const noexcept {
    return impl_->worker_executable();
}
const std::filesystem::path& PlatformWorkerSupervisor::project_root() const noexcept {
    return impl_->project_root();
}

}  // namespace biocore::infrastructure
