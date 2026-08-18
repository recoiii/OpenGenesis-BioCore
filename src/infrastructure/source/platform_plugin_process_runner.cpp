#include "biocore/infrastructure/platform_plugin_process_runner.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cerrno>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "biocore/infrastructure/windows_process_arguments.hpp"
#include "biocore/infrastructure/filesystem_plugin_registry.hpp"
#include "biocore/domain/plugin_manifest.hpp"
#include "biocore/plugin_protocol/plugin_document_codec.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace biocore::infrastructure {
namespace {

[[nodiscard]] bool is_blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
               return std::isspace(static_cast<unsigned char>(character)) != 0;
           });
}

[[nodiscard]] std::filesystem::path path_from_utf8(const std::string_view value) {
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const char character : value) {
        utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
    }
    return std::filesystem::path{utf8};
}

[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string value = path.u8string();
    return std::string{reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] bool is_within(
    const std::filesystem::path& parent,
    const std::filesystem::path& child
) {
    const std::filesystem::path relative = child.lexically_relative(parent);
    if (relative.empty() || relative.is_absolute()) return false;
    const auto first = relative.begin();
    return first != relative.end() && *first != "..";
}

[[nodiscard]] std::pair<std::filesystem::path, std::filesystem::path> validate_paths(
    const std::string_view executable_path,
    const std::string_view plugin_root_path
) {
    if (is_blank(executable_path) || is_blank(plugin_root_path) ||
        executable_path.find('\0') != std::string_view::npos ||
        plugin_root_path.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("Plugin process path is invalid");
    }
    const auto root_input = path_from_utf8(plugin_root_path);
    const auto executable_input = path_from_utf8(executable_path);
    if (!root_input.is_absolute() || !executable_input.is_absolute()) {
        throw std::invalid_argument("Plugin process paths must be absolute");
    }
    std::error_code error;
    const auto root_status = std::filesystem::symlink_status(root_input, error);
    if (error || std::filesystem::is_symlink(root_status)) {
        throw std::invalid_argument("Plugin root must not be a symbolic link");
    }
    const auto root = std::filesystem::canonical(root_input, error);
    if (error || root_input.lexically_normal() != root ||
        !std::filesystem::is_directory(root, error) || error || root == root.root_path()) {
        throw std::invalid_argument("Plugin root must be a canonical directory");
    }
    const auto executable_status = std::filesystem::symlink_status(executable_input, error);
    if (error || std::filesystem::is_symlink(executable_status)) {
        throw std::invalid_argument("Plugin executable must not be a symbolic link");
    }
    const auto executable = std::filesystem::canonical(executable_input, error);
    if (error || executable_input.lexically_normal() != executable ||
        !std::filesystem::is_regular_file(executable, error) || error ||
        !is_within(root, executable)) {
        throw std::invalid_argument("Plugin executable must be a canonical plugin-local file");
    }
#if !defined(_WIN32)
    if (::access(executable.c_str(), X_OK) != 0) {
        throw std::invalid_argument("Plugin executable is not executable");
    }
#endif
    return {root, executable};
}

[[nodiscard]] std::string read_plugin_manifest(const std::filesystem::path& plugin_root) {
    const std::filesystem::path manifest_path = plugin_root / "plugin.json";
    std::error_code error;
    const auto status = std::filesystem::symlink_status(manifest_path, error);
    if (error || std::filesystem::is_symlink(status)) {
        throw std::invalid_argument("Plugin manifest must not be a symbolic link");
    }
    const auto canonical = std::filesystem::canonical(manifest_path, error);
    if (error || manifest_path.lexically_normal() != canonical ||
        !std::filesystem::is_regular_file(canonical, error) || error ||
        !is_within(plugin_root, canonical)) {
        throw std::invalid_argument("Plugin manifest must be a canonical plugin-local file");
    }
    const auto size = std::filesystem::file_size(canonical, error);
    if (error || size == 0U || size > plugin_protocol::maximum_plugin_manifest_bytes) {
        throw std::invalid_argument("Plugin manifest file size is invalid");
    }
    std::ifstream input{canonical, std::ios::binary};
    if (!input) throw std::runtime_error("Unable to open plugin manifest");
    std::string content{
        std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}
    };
    if (input.bad()) throw std::runtime_error("Unable to read plugin manifest");
    return content;
}

[[nodiscard]] std::filesystem::path path_from_manifest_relative(
    const std::string_view value
) {
    std::filesystem::path result;
    std::size_t begin = 0U;
    while (begin < value.size()) {
        const std::size_t end = value.find('/', begin);
        const std::string_view segment = value.substr(
            begin, end == std::string_view::npos ? value.size() - begin : end - begin
        );
        std::u8string utf8;
        utf8.reserve(segment.size());
        for (const char character : segment) {
            utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
        }
        result /= std::filesystem::path{utf8};
        if (end == std::string_view::npos) break;
        begin = end + 1U;
    }
    return result;
}

void verify_manifest_binding(
    const std::filesystem::path& plugin_root,
    const std::filesystem::path& executable,
    const std::string_view plugin_id,
    const std::string_view plugin_version,
    const std::string_view module_id
) {
    const auto document = plugin_protocol::parse_plugin_manifest_document(
        read_plugin_manifest(plugin_root)
    );
    if (document.id != plugin_id || document.version != plugin_version ||
        document.api_version != domain::PluginManifest::supported_api_version) {
        throw std::invalid_argument("Execution-plan plugin identity does not match its manifest");
    }
    const std::string platform{domain::to_string(current_plugin_platform())};
    for (const auto& module : document.modules) {
        if (module.id != module_id) continue;
        if (module.type != "process") {
            throw std::invalid_argument("Execution-plan plugin module type is unsupported");
        }
        for (const auto& entrypoint : module.entrypoints) {
            if (entrypoint.platform != platform) continue;
            const domain::PluginModuleDefinition validated_module{
                module.id,
                domain::PluginModuleType::process,
                {domain::PluginEntrypoint{
                    .platform = current_plugin_platform(),
                    .relative_path = entrypoint.relative_path,
                }},
            };
            const auto safe_path = validated_module.entrypoint_for(current_plugin_platform());
            if (!safe_path.has_value()) break;
            const std::filesystem::path expected = std::filesystem::canonical(
                plugin_root / path_from_manifest_relative(*safe_path)
            );
            if (expected != executable) {
                throw std::invalid_argument(
                    "Execution-plan executable does not match the plugin manifest"
                );
            }
            return;
        }
        throw std::invalid_argument("Plugin module has no entrypoint for the current platform");
    }
    throw std::invalid_argument("Execution-plan module is absent from the plugin manifest");
}

[[nodiscard]] std::filesystem::path validate_invocation_snapshot(
    const std::string_view value
) {
    if (value.empty() || value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument("Plugin invocation snapshot path is invalid");
    }
    const auto input = path_from_utf8(value);
    if (!input.is_absolute()) {
        throw std::invalid_argument("Plugin invocation snapshot path must be absolute");
    }
    std::error_code error;
    const auto status = std::filesystem::symlink_status(input, error);
    if (error || std::filesystem::is_symlink(status)) {
        throw std::invalid_argument("Plugin invocation snapshot must not be a symbolic link");
    }
    const auto canonical = std::filesystem::canonical(input, error);
    if (error || input.lexically_normal() != canonical ||
        !std::filesystem::is_regular_file(canonical, error) || error) {
        throw std::invalid_argument("Plugin invocation snapshot must be a canonical regular file");
    }
    return canonical;
}

void validate_text(const std::string_view value, const std::string_view field) {
    if (is_blank(value) || value.size() > 256U ||
        value.find('\0') != std::string_view::npos) {
        throw std::invalid_argument(std::string{field} + " is invalid");
    }
}

#if defined(_WIN32)

[[nodiscard]] std::wstring utf8_to_wide(const std::string_view value) {
    if (value.empty()) return {};
    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("Plugin process argument is too long");
    }
    const int source_size = static_cast<int>(value.size());
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), source_size, nullptr, 0
    );
    if (required <= 0) throw std::invalid_argument("Plugin process argument is not UTF-8");
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), source_size, result.data(), required
        ) != required) {
        throw std::runtime_error("Plugin process argument conversion failed");
    }
    return result;
}

class WindowsHandle final {
public:
    explicit WindowsHandle(HANDLE handle = nullptr) noexcept : handle_{handle} {}
    ~WindowsHandle() { if (valid()) CloseHandle(handle_); }
    WindowsHandle(const WindowsHandle&) = delete;
    WindowsHandle& operator=(const WindowsHandle&) = delete;
    WindowsHandle(WindowsHandle&& other) noexcept : handle_{std::exchange(other.handle_, nullptr)} {}
    WindowsHandle& operator=(WindowsHandle&& other) noexcept {
        if (this != &other) {
            if (valid()) CloseHandle(handle_);
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }
    [[nodiscard]] bool valid() const noexcept {
        return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
    }
private:
    HANDLE handle_;
};

[[nodiscard]] int run_windows(
    const std::filesystem::path& executable,
    const std::string_view module_id,
    const std::string_view step_id,
    const std::string_view invocation_snapshot_path
) {
    const std::vector<std::wstring> arguments{
        executable.native(), L"--module-id", utf8_to_wide(module_id),
        L"--step-id", utf8_to_wide(step_id),
        L"--invocation", utf8_to_wide(invocation_snapshot_path),
    };
    std::wstring command_line;
    for (std::size_t index = 0U; index < arguments.size(); ++index) {
        if (index != 0U) command_line.push_back(L' ');
        command_line += quote_windows_process_argument(arguments[index]);
    }

    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    WindowsHandle null_input{CreateFileW(
        L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr
    )};
    WindowsHandle null_output{CreateFileW(
        L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr
    )};
    if (!null_input.valid() || !null_output.valid()) {
        throw std::system_error(
            static_cast<int>(GetLastError()), std::system_category(),
            "Unable to open Windows NUL handles for plugin process"
        );
    }

    SIZE_T attribute_size = 0U;
    static_cast<void>(InitializeProcThreadAttributeList(nullptr, 1U, 0U, &attribute_size));
    std::vector<std::byte> attribute_storage(attribute_size);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1U, 0U, &attribute_size)) {
        throw std::system_error(
            static_cast<int>(GetLastError()), std::system_category(),
            "Unable to initialize plugin process handle list"
        );
    }
    struct AttributeGuard final {
        LPPROC_THREAD_ATTRIBUTE_LIST value;
        ~AttributeGuard() { DeleteProcThreadAttributeList(value); }
    } attribute_guard{attributes};
    std::array<HANDLE, 2U> handles{null_input.get(), null_output.get()};
    if (!UpdateProcThreadAttribute(
            attributes, 0U, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles.data(),
            sizeof(handles), nullptr, nullptr
        )) {
        throw std::system_error(
            static_cast<int>(GetLastError()), std::system_category(),
            "Unable to restrict plugin process handles"
        );
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = null_input.get();
    startup.StartupInfo.hStdOutput = null_output.get();
    startup.StartupInfo.hStdError = null_output.get();
    startup.lpAttributeList = attributes;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            executable.c_str(), command_line.data(), nullptr, nullptr, TRUE,
            EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr,
            &startup.StartupInfo, &process
        )) {
        throw std::system_error(
            static_cast<int>(GetLastError()), std::system_category(),
            "Unable to launch plugin process"
        );
    }
    WindowsHandle process_handle{process.hProcess};
    WindowsHandle thread_handle{process.hThread};
    if (WaitForSingleObject(process_handle.get(), INFINITE) != WAIT_OBJECT_0) {
        throw std::system_error(
            static_cast<int>(GetLastError()), std::system_category(),
            "Unable to wait for plugin process"
        );
    }
    DWORD exit_code = 0U;
    if (!GetExitCodeProcess(process_handle.get(), &exit_code)) {
        throw std::system_error(
            static_cast<int>(GetLastError()), std::system_category(),
            "Unable to read plugin process exit code"
        );
    }
    return static_cast<int>(exit_code);
}

#else

[[nodiscard]] int run_posix(
    const std::filesystem::path& executable,
    const std::string_view module_id,
    const std::string_view step_id,
    const std::string_view invocation_snapshot_path
) {
    std::vector<std::string> arguments{
        path_to_utf8(executable), "--module-id", std::string{module_id},
        "--step-id", std::string{step_id},
        "--invocation", std::string{invocation_snapshot_path},
    };
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1U);
    for (std::string& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions{};
    int result = posix_spawn_file_actions_init(&actions);
    if (result != 0) {
        throw std::system_error(result, std::generic_category(), "Unable to initialize plugin spawn actions");
    }
    struct ActionsGuard final {
        posix_spawn_file_actions_t* actions;
        ~ActionsGuard() { static_cast<void>(posix_spawn_file_actions_destroy(actions)); }
    } guard{&actions};
    result = posix_spawn_file_actions_addopen(
        &actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0
    );
    if (result == 0) {
        result = posix_spawn_file_actions_addopen(
            &actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0
        );
    }
    if (result != 0) {
        throw std::system_error(result, std::generic_category(), "Unable to configure plugin process streams");
    }

    pid_t process_id = -1;
    result = posix_spawn(
        &process_id, executable.c_str(), &actions, nullptr, argv.data(), environ
    );
    if (result != 0) {
        throw std::system_error(result, std::generic_category(), "Unable to launch plugin process");
    }

    int status = 0;
    for (;;) {
        const pid_t waited = waitpid(process_id, &status, 0);
        if (waited == process_id) break;
        if (waited < 0 && errno == EINTR) continue;
        if (waited < 0) {
            throw std::system_error(errno, std::generic_category(), "Unable to wait for plugin process");
        }
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    throw std::runtime_error("Plugin process ended in an unsupported state");
}

#endif

}  // namespace

int PlatformPluginProcessRunner::run(
    const std::string_view executable_path,
    const std::string_view plugin_root_path,
    const std::string_view plugin_id,
    const std::string_view plugin_version,
    const std::string_view module_id,
    const std::string_view step_id,
    const std::string_view invocation_snapshot_path
) {
    validate_text(plugin_id, "Plugin id");
    validate_text(plugin_version, "Plugin version");
    validate_text(module_id, "Plugin module id");
    validate_text(step_id, "Pipeline step id");
    const auto invocation_snapshot = validate_invocation_snapshot(invocation_snapshot_path);
    const auto [plugin_root, executable] = validate_paths(executable_path, plugin_root_path);
    verify_manifest_binding(
        plugin_root, executable, plugin_id, plugin_version, module_id
    );
#if defined(_WIN32)
    return run_windows(executable, module_id, step_id, path_to_utf8(invocation_snapshot));
#else
    return run_posix(executable, module_id, step_id, path_to_utf8(invocation_snapshot));
#endif
}

}  // namespace biocore::infrastructure
