#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include "biocore/domain/plugin_platform.hpp"
#include "biocore/infrastructure/filesystem_plugin_registry.hpp"

namespace {

namespace fs = std::filesystem;
using biocore::infrastructure::FilesystemPluginRegistry;

[[nodiscard]] fs::path path_from_utf8(const std::string_view value) {
    std::u8string utf8;
    utf8.reserve(value.size());
    for (const char character : value) {
        utf8.push_back(static_cast<char8_t>(static_cast<unsigned char>(character)));
    }
    return fs::path{utf8};
}

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        path_ = fs::temp_directory_path() /
                ("biocore-plugin-registry-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()
                ));
        fs::create_directories(path_);
        path_ = fs::canonical(path_);
    }
    ~TemporaryDirectory() {
        std::error_code error;
        fs::remove_all(path_, error);
    }
    [[nodiscard]] const fs::path& path() const noexcept { return path_; }
private:
    fs::path path_;
};

void write_file(const fs::path& path, const std::string_view content) {
    fs::create_directories(path.parent_path());
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output) throw std::runtime_error("Unable to write plugin test file");
}

[[nodiscard]] std::string manifest_json(
    const std::string_view plugin_id,
    const std::string_view module_id,
    const std::string_view platform,
    const std::string_view entrypoint
) {
    return "{\"manifestVersion\":1,\"id\":\"" + std::string{plugin_id} +
           "\",\"name\":\"Test Plugin\",\"version\":\"0.1.0\","
           "\"apiVersion\":\"1.0\",\"publisher\":\"BioCore\",\"modules\":[{"
           "\"id\":\"" + std::string{module_id} +
           "\",\"type\":\"process\",\"entrypoints\":{\"" +
           std::string{platform} + "\":\"" + std::string{entrypoint} + "\"}}]}";
}

[[nodiscard]] fs::path make_plugin(
    const fs::path& root,
    const fs::path& source_executable,
    const std::string_view directory_name,
    const std::string_view plugin_id,
    const std::string_view module_id
) {
    const std::string platform{
        biocore::domain::to_string(biocore::infrastructure::current_plugin_platform())
    };
    const fs::path plugin = root / std::string{directory_name};
    const fs::path executable = plugin / "bin" / platform / source_executable.filename();
    fs::create_directories(executable.parent_path());
    fs::copy_file(source_executable, executable, fs::copy_options::overwrite_existing);
#if !defined(_WIN32)
    fs::permissions(
        executable,
        fs::perms::owner_exec | fs::perms::owner_read | fs::perms::owner_write |
            fs::perms::group_exec | fs::perms::group_read,
        fs::perm_options::replace
    );
#endif
    const std::string relative = "bin/" + platform + "/" +
                                 executable.filename().string();
    write_file(
        plugin / "plugin.json",
        manifest_json(plugin_id, module_id, platform, relative)
    );
    return plugin;
}

[[nodiscard]] bool discovery_and_isolation_contract(const fs::path& executable) {
    TemporaryDirectory temporary;
    static_cast<void>(make_plugin(
        temporary.path(), executable, "valid", "org.biocore.valid", "org.biocore.valid.module"
    ));
    const fs::path invalid = temporary.path() / "invalid";
    fs::create_directory(invalid);
    write_file(invalid / "plugin.json", "{not-json");

    FilesystemPluginRegistry registry{{temporary.path()}};
    const auto report = registry.refresh();
    const auto module = registry.find_module("org.biocore.valid.module");
    return report.loaded_plugins == 1U && report.loaded_modules == 1U &&
           report.rejected.size() == 1U && module.has_value() &&
           fs::is_regular_file(path_from_utf8(module->executable_path)) &&
           registry.list_plugins().size() == 1U;
}

[[nodiscard]] bool duplicate_conflict_contract(const fs::path& executable) {
    TemporaryDirectory temporary;
    static_cast<void>(make_plugin(
        temporary.path(), executable, "one", "org.biocore.duplicate", "org.biocore.duplicate.module"
    ));
    static_cast<void>(make_plugin(
        temporary.path(), executable, "two", "org.biocore.duplicate", "org.biocore.duplicate.module"
    ));
    FilesystemPluginRegistry registry{{temporary.path()}};
    const auto report = registry.refresh();
    return report.loaded_plugins == 0U && report.loaded_modules == 0U &&
           report.rejected.size() == 2U && registry.list_plugins().empty() &&
           !registry.find_module("org.biocore.duplicate.module").has_value();
}

[[nodiscard]] bool missing_platform_contract(const fs::path& executable) {
    TemporaryDirectory temporary;
    const fs::path plugin = make_plugin(
        temporary.path(), executable, "missing", "org.biocore.missing", "org.biocore.missing.module"
    );
    write_file(
        plugin / "plugin.json",
        manifest_json(
            "org.biocore.missing", "org.biocore.missing.module", "windows-arm64",
            "bin/windows-arm64/missing.exe"
        )
    );
    FilesystemPluginRegistry registry{{temporary.path()}};
    const auto report = registry.refresh();
    return report.loaded_plugins == 0U && report.rejected.size() == 1U;
}

[[nodiscard]] bool symlink_entrypoint_contract(const fs::path& executable) {
#if defined(_WIN32)
    static_cast<void>(executable);
    return true;
#else
    TemporaryDirectory temporary;
    const std::string platform{
        biocore::domain::to_string(biocore::infrastructure::current_plugin_platform())
    };
    const fs::path plugin = temporary.path() / "symlink";
    const fs::path link = plugin / "bin" / platform / "demo-link";
    fs::create_directories(link.parent_path());
    fs::create_symlink(executable, link);
    write_file(
        plugin / "plugin.json",
        manifest_json(
            "org.biocore.symlink", "org.biocore.symlink.module", platform,
            "bin/" + platform + "/demo-link"
        )
    );
    FilesystemPluginRegistry registry{{temporary.path()}};
    const auto report = registry.refresh();
    return report.loaded_plugins == 0U && report.rejected.size() == 1U;
#endif
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    if (argc != 2) {
        std::cerr << "Expected demo plugin executable path\n";
        return EXIT_FAILURE;
    }
    const fs::path executable = fs::canonical(path_from_utf8(argv[1]));
    if (!discovery_and_isolation_contract(executable) ||
        !duplicate_conflict_contract(executable) ||
        !missing_platform_contract(executable) ||
        !symlink_entrypoint_contract(executable)) {
        std::cerr << "Filesystem plugin registry tests failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Filesystem plugin registry tests passed\n";
    return EXIT_SUCCESS;
}
