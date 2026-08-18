#pragma once

#include <filesystem>

#include "biocore/application/i_partial_output_cleaner.hpp"

namespace biocore::infrastructure {

class FilesystemPartialOutputCleaner final : public application::IPartialOutputCleaner {
public:
    explicit FilesystemPartialOutputCleaner(std::filesystem::path project_root);

    [[nodiscard]] application::PartialOutputCleanupResult quarantine_unregistered_outputs(
        std::string_view job_id,
        std::span<const std::string> protected_relative_paths
    ) override;

private:
    std::filesystem::path project_root_;
    std::filesystem::path outputs_directory_;
    std::filesystem::path metadata_directory_;
};

}  // namespace biocore::infrastructure
