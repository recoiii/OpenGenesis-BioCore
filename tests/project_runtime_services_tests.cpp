#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

#include "biocore/infrastructure/filesystem_path_canonicalizer.hpp"
#include "biocore/infrastructure/system_clock.hpp"
#include "biocore/infrastructure/uuid_v4_generator.hpp"

namespace {

using biocore::infrastructure::FilesystemPathCanonicalizer;
using biocore::infrastructure::SystemClock;
using biocore::infrastructure::UuidV4Generator;

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        const auto unique_value = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("biocore-project-runtime-test-" + std::to_string(unique_value));
        std::filesystem::create_directories(path_ / std::filesystem::path{u8"veri-ü"} / "nested");
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::string to_utf8(const std::filesystem::path& path) {
    const std::u8string encoded = path.generic_u8string();
    std::string result;
    result.reserve(encoded.size());
    for (const char8_t character : encoded) {
        result.push_back(static_cast<char>(character));
    }
    return result;
}

[[nodiscard]] bool throws_for_path(FilesystemPathCanonicalizer& canonicalizer, const std::string_view path) {
    try {
        (void)canonicalizer.canonicalize_existing_directory(path);
    } catch (const std::invalid_argument&) {
        return true;
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

[[nodiscard]] bool verifies_uuid_v4() {
    UuidV4Generator generator;
    const std::regex format{
        "^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$"};
    std::set<std::string> identifiers;

    for (std::size_t index = 0; index < 512U; ++index) {
        const std::string identifier = generator.generate();
        if (!std::regex_match(identifier, format)) {
            return false;
        }
        identifiers.insert(identifier);
    }

    return identifiers.size() == 512U;
}

[[nodiscard]] bool verifies_system_clock_format() {
    SystemClock clock;
    const std::regex format{"^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}Z$"};
    return std::regex_match(clock.now_utc_iso8601(), format);
}

[[nodiscard]] bool verifies_path_canonicalization() {
    TemporaryDirectory temporary;
    FilesystemPathCanonicalizer canonicalizer;
    const std::filesystem::path target = temporary.path() / std::filesystem::path{u8"veri-ü"};
    const std::filesystem::path alias = target / "nested" / ".." / ".";

    const std::string canonical_target = canonicalizer.canonicalize_existing_directory(to_utf8(target));
    const std::string canonical_alias = canonicalizer.canonicalize_existing_directory(to_utf8(alias));
    if (canonical_target != canonical_alias || canonical_target.find("veri-") == std::string::npos) {
        return false;
    }

    const std::filesystem::path regular_file = temporary.path() / "not-a-directory.txt";
    {
        std::ofstream output{regular_file};
        output << "test";
    }

    if (!throws_for_path(canonicalizer, "") || !throws_for_path(canonicalizer, "   ") ||
        !throws_for_path(canonicalizer, to_utf8(temporary.path() / "missing")) ||
        !throws_for_path(canonicalizer, to_utf8(regular_file)) ||
        !throws_for_path(canonicalizer, std::string{"root\0hidden", 11U})) {
        return false;
    }

#ifndef _WIN32
    const std::filesystem::path symlink_path = temporary.path() / "project-link";
    std::error_code symlink_error;
    std::filesystem::create_directory_symlink(target, symlink_path, symlink_error);
    if (!symlink_error &&
        canonicalizer.canonicalize_existing_directory(to_utf8(symlink_path)) != canonical_target) {
        return false;
    }
#endif

    return true;
}

}  // namespace

int main() {
    if (!verifies_uuid_v4()) {
        std::cerr << "UUID v4 generator contract failed\n";
        return EXIT_FAILURE;
    }
    if (!verifies_system_clock_format()) {
        std::cerr << "System UTC clock format contract failed\n";
        return EXIT_FAILURE;
    }
    if (!verifies_path_canonicalization()) {
        std::cerr << "Filesystem path canonicalization contract failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
