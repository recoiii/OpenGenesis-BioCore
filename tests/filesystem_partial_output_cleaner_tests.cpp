#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "biocore/infrastructure/filesystem_partial_output_cleaner.hpp"

namespace {
namespace fs = std::filesystem;

class TempProject final {
public:
    TempProject() {
        root_ = fs::temp_directory_path() /
                ("biocore-partial-cleaner-" + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root_ / ".biocore");
        fs::create_directories(root_ / "outputs");
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

void write(const fs::path& path, const std::string& value) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    output << value;
    if (!output) throw std::runtime_error("Unable to create cleaner fixture");
}

[[nodiscard]] bool quarantine_contract() {
    TempProject project;
    const auto outputs = project.root() / "outputs";
    const auto protected_file = outputs / "job-1--done--result.out";
    const auto partial_file = outputs / "job-1--bad--partial.out";
    const auto other_job = outputs / "job-2--bad--partial.out";
    write(protected_file, "good");
    write(partial_file, "partial");
    write(other_job, "other");

    const auto outside = project.root() / "outside.txt";
    write(outside, "outside");
    const auto suspicious = outputs / "job-1--bad--link.out";
    std::error_code symlink_error;
    fs::create_symlink(outside, suspicious, symlink_error);
    const bool symlink_created = !symlink_error;

    biocore::infrastructure::FilesystemPartialOutputCleaner cleaner{project.root()};
    const std::vector<std::string> protected_paths{
        "outputs/job-1--done--result.out"
    };
    const auto result = cleaner.quarantine_unregistered_outputs("job-1", protected_paths);

    if (result.quarantined.size() != 1U ||
        result.quarantined.front().original_relative_path !=
            "outputs/job-1--bad--partial.out" ||
        fs::exists(partial_file) || !fs::is_regular_file(protected_file) ||
        !fs::is_regular_file(other_job)) {
        return false;
    }
    const auto quarantined = project.root() / result.quarantined.front().quarantine_relative_path;
    if (!fs::is_regular_file(quarantined)) return false;

    if (symlink_created) {
        if (!fs::is_symlink(suspicious) || result.skipped_relative_paths.size() != 1U ||
            result.skipped_relative_paths.front() != "outputs/job-1--bad--link.out") {
            return false;
        }
    } else if (!result.skipped_relative_paths.empty()) {
        return false;
    }

    const auto second = cleaner.quarantine_unregistered_outputs("job-1", protected_paths);
    return second.quarantined.empty() &&
           second.skipped_relative_paths.size() == (symlink_created ? 1U : 0U);
}

[[nodiscard]] bool unsafe_job_id_rejected() {
    TempProject project;
    biocore::infrastructure::FilesystemPartialOutputCleaner cleaner{project.root()};
    try {
        static_cast<void>(cleaner.quarantine_unregistered_outputs("../escape", {}));
        return false;
    } catch (const std::invalid_argument&) {
        return true;
    }
}

}  // namespace

int main() {
    if (!quarantine_contract() || !unsafe_job_id_rejected()) {
        std::cerr << "Filesystem partial output cleaner tests failed\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
