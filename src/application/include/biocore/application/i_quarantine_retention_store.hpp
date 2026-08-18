#pragma once

#include <chrono>
#include <string>
#include <vector>

namespace biocore::application {

struct QuarantineRetentionResult final {
    std::vector<std::string> purged_relative_paths;
    std::vector<std::string> skipped_relative_paths;
};

class IQuarantineRetentionStore {
public:
    virtual ~IQuarantineRetentionStore() = default;

    [[nodiscard]] virtual QuarantineRetentionResult purge_expired(
        std::chrono::seconds minimum_age
    ) = 0;
};

}  // namespace biocore::application
