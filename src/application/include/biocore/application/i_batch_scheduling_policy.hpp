#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace biocore::application {

struct BatchSchedulingQuota final {
    std::string group_id;
    std::size_t maximum_concurrent_jobs{1U};
};

class IBatchSchedulingPolicy {
public:
    virtual ~IBatchSchedulingPolicy() = default;

    [[nodiscard]] virtual std::optional<BatchSchedulingQuota> quota_for_job(
        std::string_view job_id
    ) = 0;
};

}  // namespace biocore::application
