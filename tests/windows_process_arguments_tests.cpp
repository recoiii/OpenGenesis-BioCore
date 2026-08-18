#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>

#include "biocore/infrastructure/windows_process_arguments.hpp"

int main() {
    using biocore::infrastructure::make_windows_process_command_line;
    using biocore::infrastructure::quote_windows_process_argument;

    constexpr std::array cases{
        std::pair{std::wstring_view{L""}, std::wstring_view{L"\"\""}},
        std::pair{std::wstring_view{L"simple"}, std::wstring_view{L"\"simple\""}},
        std::pair{std::wstring_view{L"two words"}, std::wstring_view{L"\"two words\""}},
        std::pair{std::wstring_view{L"a\"b"}, std::wstring_view{L"\"a\\\"b\""}},
        std::pair{std::wstring_view{L"C:\\path\\"}, std::wstring_view{L"\"C:\\path\\\\\""}},
        std::pair{
            std::wstring_view{L"two\\\\\"quotes"},
            std::wstring_view{L"\"two\\\\\\\\\\\"quotes\""}
        },
    };

    for (const auto& [input, expected] : cases) {
        if (quote_windows_process_argument(input) != expected) {
            std::wcerr << L"Windows argument quoting mismatch for: " << input << L'\n';
            return EXIT_FAILURE;
        }
    }

    const std::array<std::wstring, 4U> arguments{
        L"C:\\Program Files\\OpenGenesis-BioCore\\biocore-worker.exe",
        L"--job-id",
        L"job & quoted \"value\"",
        L"",
    };
    const std::wstring expected =
        L"\"C:\\Program Files\\OpenGenesis-BioCore\\biocore-worker.exe\" "
        L"\"--job-id\" \"job & quoted \\\"value\\\"\" \"\"";
    if (make_windows_process_command_line(arguments) != expected) {
        std::wcerr << L"Windows command line assembly mismatch\n";
        return EXIT_FAILURE;
    }
    const std::filesystem::path packaging_path =
        std::filesystem::path{BIOCORE_SOURCE_DIR} / "cmake" / "BioCoreWindowsPackaging.cmake";
    std::ifstream packaging_stream{packaging_path, std::ios::binary};
    if (!packaging_stream) {
        std::cerr << "Could not read Windows packaging policy: " << packaging_path << '\n';
        return EXIT_FAILURE;
    }
    const std::string packaging{
        std::istreambuf_iterator<char>{packaging_stream}, std::istreambuf_iterator<char>{}};

    constexpr std::array<std::string_view, 8U> native_plugin_ids{
        "org.biocore.demo",
        "org.biocore.fastaqc",
        "org.biocore.fastqqc",
        "org.biocore.align",
        "org.biocore.alignmentqc",
        "org.biocore.variantcall",
        "org.biocore.vcfqc",
        "org.biocore.variantannotate",
    };
    for (const std::string_view plugin_id : native_plugin_ids) {
        const std::string exact_list_entry = "\n        " + std::string{plugin_id} + "\n";
        if (packaging.find(exact_list_entry) == std::string::npos) {
            std::cerr << "Windows packaging policy is missing native plugin: " << plugin_id << '\n';
            return EXIT_FAILURE;
        }
    }

    constexpr std::array<std::string_view, 2U> forbidden_system_directory_guards{
        "[Ss][Yy][Ss][Tt][Ee][Mm]32",
        "[Ss][Yy][Ss][Ww][Oo][Ww]64",
    };
    for (const std::string_view guard : forbidden_system_directory_guards) {
        if (packaging.find(guard) == std::string::npos) {
            std::cerr << "Windows packaging policy lost system-directory exclusion: " << guard << '\n';
            return EXIT_FAILURE;
        }
    }

    constexpr std::array<std::string_view, 2U> app_local_runtime_distribution_contract{
        "foreach(BIOCORE_WINDOWS_NATIVE_PLUGIN_ID IN LISTS BIOCORE_WINDOWS_NATIVE_PLUGIN_IDS)",
        "${BIOCORE_WINDOWS_NATIVE_PLUGIN_ID}/bin/windows-x64",
    };
    for (const std::string_view token : app_local_runtime_distribution_contract) {
        if (packaging.find(token) == std::string::npos) {
            std::cerr << "Windows packaging policy lost app-local runtime distribution contract: "
                      << token << '\n';
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
