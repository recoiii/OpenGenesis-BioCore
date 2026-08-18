#pragma once

#include "biocore/application/i_monotonic_clock.hpp"

namespace biocore::infrastructure {

class MonotonicClock final : public application::IMonotonicClock {
public:
    [[nodiscard]] std::chrono::steady_clock::time_point now() override;
};

}  // namespace biocore::infrastructure
