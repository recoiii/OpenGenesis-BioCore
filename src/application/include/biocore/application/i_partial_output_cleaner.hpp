#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace biocore::application {

struct QuarantinedPartialOutput final {
    std::string original_relative_path;
    std::string quarantine_relative_path;
};

struct PartialOutputCleanupResult final {
    std::vector<QuarantinedPartialOutput> quarantined;
    std::vector<std::string> skipped_relative_paths;
};

class IPartialOutputCleaner {
public:
    virtual ~IPartialOutputCleaner() = default;

    [[nodiscard]] virtual PartialOutputCleanupResult quarantine_unregistered_outputs(
        std::string_view job_id,
        std::span<const std::string> protected_relative_paths
    ) = 0;
};

}  // namespace biocore::application
