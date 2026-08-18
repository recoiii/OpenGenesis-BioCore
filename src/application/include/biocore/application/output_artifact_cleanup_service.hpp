#pragma once

#include <string_view>

#include "biocore/application/i_partial_output_cleaner.hpp"

namespace biocore::application {

class IManagedFileRepository;

class OutputArtifactCleanupService final {
public:
    OutputArtifactCleanupService(
        IManagedFileRepository& repository,
        IPartialOutputCleaner& cleaner
    ) noexcept;

    [[nodiscard]] PartialOutputCleanupResult quarantine_unregistered_for_job(
        std::string_view job_id
    );

private:
    IManagedFileRepository& repository_;
    IPartialOutputCleaner& cleaner_;
};

}  // namespace biocore::application
