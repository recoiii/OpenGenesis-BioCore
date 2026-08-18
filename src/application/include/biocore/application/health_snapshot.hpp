#pragma once

#include <string>

namespace biocore::application {

struct HealthSnapshot final {
    std::string status;
    std::string component;
    std::string version;
    std::string timestamp_utc;
};

}  // namespace biocore::application
