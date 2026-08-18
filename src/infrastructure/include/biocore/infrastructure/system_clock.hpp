#pragma once

#include "biocore/application/i_utc_clock.hpp"

namespace biocore::infrastructure {

class SystemClock final : public application::IUtcClock {
public:
    [[nodiscard]] std::string now_utc_iso8601() override;
};

}  // namespace biocore::infrastructure
