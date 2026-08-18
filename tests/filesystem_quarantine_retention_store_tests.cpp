#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "biocore/infrastructure/filesystem_quarantine_retention_store.hpp"

namespace {
namespace fs = std::filesystem;

class TempProject final {
public:
    TempProject() {
        root_ = fs::temp_directory_path() /
                ("biocore-retention-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root_ / ".biocore" / "quarantine" / "outputs" / "job-1");
        root_ = fs::canonical(root_);
    }
    ~TempProject() {
        std::error_code error;
        fs::remove_all(root_, error);
    }
    [[nodiscard]] const fs::path& root() const noexcept { return root_; }

private:
    fs::path root_;
};

void write(const fs::path& path, const std::string& content) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output << content;
    if (!output) throw std::runtime_error("Unable to write retention fixture");
}

[[nodiscard]] bool retention_contract() {
    TempProject project;
    const auto job = project.root() / ".biocore" / "quarantine" / "outputs" / "job-1";
    const auto old_file = job / "old.partial";
    const auto fresh_file = job / "fresh.partial";
    write(old_file, "old");
    write(fresh_file, "fresh");

    const auto now = fs::file_time_type::clock::now();
    fs::last_write_time(old_file, now - std::chrono::hours{24 * 40});
    fs::last_write_time(fresh_file, now - std::chrono::hours{24 * 5});

    const auto outside = project.root() / "outside.txt";
    write(outside, "outside");
    const auto link = job / "link.partial";
    std::error_code symlink_error;
    fs::create_symlink(outside, link, symlink_error);
    const bool symlink_created = !symlink_error;

    biocore::infrastructure::FilesystemQuarantineRetentionStore store{project.root()};
    const auto result = store.purge_expired(std::chrono::hours{24 * 30});

    if (result.purged_relative_paths.size() != 1U ||
        result.purged_relative_paths.front() !=
            ".biocore/quarantine/outputs/job-1/old.partial" ||
        fs::exists(old_file) || !fs::is_regular_file(fresh_file) ||
        !fs::is_regular_file(outside)) {
        return false;
    }
    if (symlink_created) {
        return fs::is_symlink(link) && result.skipped_relative_paths.size() == 1U &&
               result.skipped_relative_paths.front() ==
                   ".biocore/quarantine/outputs/job-1/link.partial";
    }
    return result.skipped_relative_paths.empty();
}

[[nodiscard]] bool invalid_age_rejected() {
    TempProject project;
    biocore::infrastructure::FilesystemQuarantineRetentionStore store{project.root()};
    try {
        static_cast<void>(store.purge_expired(std::chrono::seconds{0}));
        return false;
    } catch (const std::invalid_argument&) {
        return true;
    }
}

}  // namespace

int main() {
    if (!retention_contract() || !invalid_age_rejected()) {
        std::cerr << "Filesystem quarantine retention tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
