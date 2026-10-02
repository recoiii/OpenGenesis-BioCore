#pragma once

#include <string>
#include <vector>

#include "biocore/application/cohort_registry.hpp"

namespace biocore::presentation {

[[nodiscard]] std::string render_cohort_definition(
    const application::CohortDefinition& cohort
);
[[nodiscard]] std::string render_cohort_list(
    const std::vector<application::CohortDefinition>& cohorts
);

}  // namespace biocore::presentation
