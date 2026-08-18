#pragma once

#include <filesystem>

#include "biocore/application/i_quarantine_retention_store.hpp"

namespace biocore::infrastructure {

class FilesystemQuarantineRetentionStore final : public application::IQuarantineRetentionStore {
public:
    explicit FilesystemQuarantineRetentionStore(std::filesystem::path project_root);

    [[nodiscard]] application::QuarantineRetentionResult purge_expired(
        std::chrono::seconds minimum_age
    ) override;

private:
    std::filesystem::path project_root_;
    std::filesystem::path quarantine_outputs_directory_;
};

}  // namespace biocore::infrastructure
