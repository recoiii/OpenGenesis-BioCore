#pragma once

#include <string>

namespace biocore::application {

class IUtcClock {
public:
    virtual ~IUtcClock() = default;

    [[nodiscard]] virtual std::string now_utc_iso8601() = 0;
};

}  // namespace biocore::application
