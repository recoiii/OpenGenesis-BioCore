#include "biocore/infrastructure/filesystem_quarantine_retention_store.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <system_error>

namespace biocore::infrastructure {
namespace {
namespace fs = std::filesystem;

[[nodiscard]] std::string path_to_utf8(const fs::path& path) {
#if defined(_WIN32)
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
#else
    return path.generic_string();
#endif
}

[[nodiscard]] bool is_within(const fs::path& root, const fs::path& child) {
    auto root_it = root.begin();
    auto child_it = child.begin();
    for (; root_it != root.end(); ++root_it, ++child_it) {
        if (child_it == child.end() || *root_it != *child_it) return false;
    }
    return true;
}

[[nodiscard]] bool safe_directory(const fs::path& path) {
    std::error_code error;
    const auto status = fs::symlink_status(path, error);
    return !error && fs::is_directory(status) && !fs::is_symlink(status);
}

}  // namespace

FilesystemQuarantineRetentionStore::FilesystemQuarantineRetentionStore(fs::path project_root) {
    std::error_code error;
    project_root_ = fs::canonical(std::move(project_root), error);
    if (error || !safe_directory(project_root_)) {
        throw std::invalid_argument("Quarantine retention project root must be a canonical directory");
    }
    quarantine_outputs_directory_ =
        project_root_ / ".biocore" / "quarantine" / "outputs";
}

application::QuarantineRetentionResult FilesystemQuarantineRetentionStore::purge_expired(
    const std::chrono::seconds minimum_age
) {
    if (minimum_age <= std::chrono::seconds::zero()) {
        throw std::invalid_argument("Quarantine retention age must be positive");
    }

    application::QuarantineRetentionResult result;
    std::error_code error;
    const auto root_status = fs::symlink_status(quarantine_outputs_directory_, error);
    if (error && error != std::errc::no_such_file_or_directory) {
        throw std::runtime_error("Unable to inspect quarantine outputs directory");
    }
    if (error || !fs::exists(root_status)) {
        return result;
    }
    if (!fs::is_directory(root_status) || fs::is_symlink(root_status)) {
        throw std::runtime_error("Quarantine outputs root is not a safe directory");
    }

    const auto canonical_quarantine = fs::canonical(quarantine_outputs_directory_, error);
    if (error || !is_within(project_root_, canonical_quarantine)) {
        throw std::runtime_error("Quarantine outputs root escapes the project");
    }

    const auto now = fs::file_time_type::clock::now();
    for (const auto& job_entry : fs::directory_iterator(canonical_quarantine)) {
        const auto job_status = job_entry.symlink_status(error);
        if (error) {
            throw std::runtime_error("Unable to inspect quarantine job entry");
        }
        if (!fs::is_directory(job_status) || fs::is_symlink(job_status)) {
            result.skipped_relative_paths.push_back(
                path_to_utf8(job_entry.path().lexically_relative(project_root_))
            );
            continue;
        }

        const auto canonical_job = fs::canonical(job_entry.path(), error);
        if (error || canonical_job.parent_path() != canonical_quarantine) {
            throw std::runtime_error("Quarantine job directory is not safely contained");
        }

        for (const auto& file_entry : fs::directory_iterator(canonical_job)) {
            const auto file_status = file_entry.symlink_status(error);
            if (error) {
                throw std::runtime_error("Unable to inspect quarantined output");
            }
            const auto relative = path_to_utf8(file_entry.path().lexically_relative(project_root_));
            if (!fs::is_regular_file(file_status) || fs::is_symlink(file_status)) {
                result.skipped_relative_paths.push_back(relative);
                continue;
            }

            const auto modified = file_entry.last_write_time(error);
            if (error) {
                throw std::runtime_error("Unable to inspect quarantined output timestamp");
            }
            if (modified > now || now - modified < minimum_age) {
                continue;
            }

            const auto canonical_file = fs::canonical(file_entry.path(), error);
            if (error || canonical_file.parent_path() != canonical_job) {
                throw std::runtime_error("Quarantined output is not safely contained");
            }
            if (!fs::remove(canonical_file, error) || error) {
                throw std::runtime_error("Unable to purge expired quarantined output");
            }
            result.purged_relative_paths.push_back(relative);
        }

        error.clear();
        const bool empty = fs::directory_iterator(canonical_job, error) == fs::directory_iterator{};
        if (error) {
            throw std::runtime_error("Unable to inspect quarantine job directory emptiness");
        }
        if (empty) {
            static_cast<void>(fs::remove(canonical_job, error));
            if (error) {
                throw std::runtime_error("Unable to remove empty quarantine job directory");
            }
        }
    }

    std::ranges::sort(result.purged_relative_paths);
    std::ranges::sort(result.skipped_relative_paths);
    return result;
}

}  // namespace biocore::infrastructure
