#pragma once

#include <string>

#include "biocore/application/health_snapshot.hpp"

namespace biocore::presentation {

[[nodiscard]] std::string render_health_json(const application::HealthSnapshot& snapshot);

}  // namespace biocore::presentation
