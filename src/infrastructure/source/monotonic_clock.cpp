#include "biocore/infrastructure/monotonic_clock.hpp"

namespace biocore::infrastructure {

std::chrono::steady_clock::time_point MonotonicClock::now() {
    return std::chrono::steady_clock::now();
}

}  // namespace biocore::infrastructure
