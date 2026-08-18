#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

#include "biocore/infrastructure/platform_plugin_process_runner.hpp"

namespace {

namespace fs = std::filesystem;
using biocore::infrastructure::PlatformPluginProcessRunner;

[[nodiscard]] fs::path path_from_utf8(const std::string_view value) {
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const char character : value) {
        utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
    }
    return fs::path{utf8};
}

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
    const std::u8string value = path.u8string();
    return std::string{reinterpret_cast<const char*>(value.data()), value.size()};
}

template <typename Function>
[[nodiscard]] bool rejects(Function&& function) {
    try {
        function();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

[[nodiscard]] fs::path write_invocation(
    const std::string_view module_id,
    const std::string_view step_id
) {
    const fs::path path = fs::temp_directory_path() /
        ("biocore-plugin-runner-invocation-" + std::string{step_id} + ".json");
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) throw std::runtime_error("Unable to create plugin invocation fixture");
    output << "{\"schemaVersion\":1,\"jobId\":\"job-1\",\"jobRevision\":0,\"stepId\":\""
           << step_id << "\",\"moduleId\":\"" << module_id
           << "\",\"parameters\":[],\"inputs\":[],\"outputs\":[]}";
    output.close();
    return fs::canonical(path);
}

class TemporaryFailurePlugin final {
public:
    explicit TemporaryFailurePlugin(const fs::path& source_executable) {
        root_ = fs::temp_directory_path() /
            ("biocore-plugin-runner-failure-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()
            ));
#if defined(_WIN32)
        executable_ = root_ / "bin" / "windows-x64" / "biocore-demo-plugin.exe";
        constexpr std::string_view platform = "windows-x64";
        constexpr std::string_view relative = "bin/windows-x64/biocore-demo-plugin.exe";
#else
        executable_ = root_ / "bin" / "linux-x64" / "biocore-demo-plugin";
        constexpr std::string_view platform = "linux-x64";
        constexpr std::string_view relative = "bin/linux-x64/biocore-demo-plugin";
#endif
        fs::create_directories(executable_.parent_path());
        fs::copy_file(source_executable, executable_, fs::copy_options::overwrite_existing);
#if !defined(_WIN32)
        fs::permissions(executable_, fs::perms::owner_all, fs::perm_options::replace);
#endif
        std::ofstream manifest{root_ / "plugin.json", std::ios::binary | std::ios::trunc};
        if (!manifest) throw std::runtime_error("Unable to create temporary plugin manifest");
        manifest << "{\n"
                 << "  \"manifestVersion\": 1,\n"
                 << "  \"id\": \"org.biocore.demo\",\n"
                 << "  \"name\": \"Failure Contract Plugin\",\n"
                 << "  \"version\": \"0.1.0\",\n"
                 << "  \"apiVersion\": \"1.0\",\n"
                 << "  \"publisher\": \"BioCore Tests\",\n"
                 << "  \"modules\": [{\n"
                 << "    \"id\": \"org.biocore.demo.unknown\",\n"
                 << "    \"type\": \"process\",\n"
                 << "    \"entrypoints\": {\"" << platform << "\": \""
                 << relative << "\"}\n"
                 << "  }]\n"
                 << "}\n";
        manifest.close();
        root_ = fs::canonical(root_);
        executable_ = fs::canonical(executable_);
    }

    ~TemporaryFailurePlugin() {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    [[nodiscard]] const fs::path& root() const noexcept { return root_; }
    [[nodiscard]] const fs::path& executable() const noexcept { return executable_; }

private:
    fs::path root_;
    fs::path executable_;
};

[[nodiscard]] bool execution_contract(const fs::path& executable, const fs::path& root) {
    PlatformPluginProcessRunner runner;
    const fs::path validate_invocation = write_invocation("org.biocore.demo.validate", "validate");
    const fs::path unknown_invocation = write_invocation("org.biocore.demo.unknown", "unknown");
    const bool success = runner.run(
        path_to_utf8(executable), path_to_utf8(root),
        "org.biocore.demo", "0.1.0",
        "org.biocore.demo.validate", "validate", path_to_utf8(validate_invocation)
    ) == 0;
    const bool missing_module_rejected = rejects([&] {
        static_cast<void>(runner.run(
            path_to_utf8(executable), path_to_utf8(root),
            "org.biocore.demo", "0.1.0",
            "org.biocore.demo.unknown", "unknown", path_to_utf8(unknown_invocation)
        ));
    });
    const bool version_mismatch_rejected = rejects([&] {
        static_cast<void>(runner.run(
            path_to_utf8(executable), path_to_utf8(root),
            "org.biocore.demo", "9.9.9",
            "org.biocore.demo.validate", "validate", path_to_utf8(validate_invocation)
        ));
    });
    TemporaryFailurePlugin failure_plugin{executable};
    const bool nonzero_exit_preserved = runner.run(
        path_to_utf8(failure_plugin.executable()), path_to_utf8(failure_plugin.root()),
        "org.biocore.demo", "0.1.0",
        "org.biocore.demo.unknown", "unknown", path_to_utf8(unknown_invocation)
    ) == 3;
    std::error_code cleanup_error;
    fs::remove(validate_invocation, cleanup_error);
    cleanup_error.clear();
    fs::remove(unknown_invocation, cleanup_error);
    return success && missing_module_rejected && version_mismatch_rejected &&
           nonzero_exit_preserved;
}

[[nodiscard]] bool boundary_contract(const fs::path& executable, const fs::path& root) {
    PlatformPluginProcessRunner runner;
    const fs::path invocation = write_invocation("org.biocore.demo.validate", "validate");
    const fs::path outside = fs::temp_directory_path() /
        ("biocore-plugin-runner-outside-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()
        ));
    fs::copy_file(executable, outside, fs::copy_options::overwrite_existing);
#if !defined(_WIN32)
    fs::permissions(outside, fs::perms::owner_all, fs::perm_options::replace);
#endif
    const bool outside_rejected = rejects([&] {
        static_cast<void>(runner.run(
            path_to_utf8(outside), path_to_utf8(root),
            "org.biocore.demo", "0.1.0",
            "org.biocore.demo.validate", "validate", path_to_utf8(invocation)
        ));
    });
    std::error_code error;
    fs::remove(outside, error);

#if defined(_WIN32)
    const bool symlink_rejected = true;
#else
    const fs::path link = root / "runner-symlink";
    fs::create_symlink(executable, link);
    const bool symlink_rejected = rejects([&] {
        static_cast<void>(runner.run(
            path_to_utf8(link), path_to_utf8(root),
            "org.biocore.demo", "0.1.0",
            "org.biocore.demo.validate", "validate", path_to_utf8(invocation)
        ));
    });
    fs::remove(link, error);
#endif
    const bool relative_invocation_rejected = rejects([&] {
        static_cast<void>(runner.run(
            path_to_utf8(executable), path_to_utf8(root),
            "org.biocore.demo", "0.1.0",
            "org.biocore.demo.validate", "validate", "relative-invocation.json"
        ));
    });
    const fs::path missing_invocation = invocation.parent_path() / "biocore-missing-invocation.json";
    const bool missing_invocation_rejected = rejects([&] {
        static_cast<void>(runner.run(
            path_to_utf8(executable), path_to_utf8(root),
            "org.biocore.demo", "0.1.0",
            "org.biocore.demo.validate", "validate", path_to_utf8(missing_invocation)
        ));
    });
#if defined(_WIN32)
    const bool invocation_symlink_rejected = true;
#else
    const fs::path invocation_link = invocation.parent_path() / "biocore-plugin-runner-invocation-link.json";
    fs::create_symlink(invocation, invocation_link);
    const bool invocation_symlink_rejected = rejects([&] {
        static_cast<void>(runner.run(
            path_to_utf8(executable), path_to_utf8(root),
            "org.biocore.demo", "0.1.0",
            "org.biocore.demo.validate", "validate", path_to_utf8(invocation_link)
        ));
    });
    fs::remove(invocation_link, error);
#endif
    fs::remove(invocation, error);
    return outside_rejected && symlink_rejected && relative_invocation_rejected &&
           missing_invocation_rejected && invocation_symlink_rejected;
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    if (argc != 2) {
        std::cerr << "Expected demo plugin executable path\n";
        return EXIT_FAILURE;
    }
    const fs::path executable = fs::canonical(path_from_utf8(argv[1]));
    const fs::path root = executable.parent_path().parent_path().parent_path();
    if (!execution_contract(executable, root) || !boundary_contract(executable, root)) {
        std::cerr << "Platform plugin process runner tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Platform plugin process runner tests passed\n";
    return EXIT_SUCCESS;
}
