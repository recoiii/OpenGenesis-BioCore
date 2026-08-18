#pragma once

#include <chrono>

namespace biocore::application {

class IMonotonicClock {
public:
    virtual ~IMonotonicClock() = default;
    [[nodiscard]] virtual std::chrono::steady_clock::time_point now() = 0;
};

}  // namespace biocore::application
