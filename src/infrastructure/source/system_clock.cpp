#include "biocore/infrastructure/system_clock.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace biocore::infrastructure {

std::string SystemClock::now_utc_iso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};

#ifdef _WIN32
    if (gmtime_s(&utc, &time) != 0) {
        throw std::runtime_error("Unable to convert system time to UTC");
    }
#else
    if (gmtime_r(&time, &utc) == nullptr) {
        throw std::runtime_error("Unable to convert system time to UTC");
    }
#endif

    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

}  // namespace biocore::infrastructure
