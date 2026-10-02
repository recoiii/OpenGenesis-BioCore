#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/cohort_registry.hpp"

namespace biocore::presentation {

[[nodiscard]] std::string render_cohort_definition(
    const application::CohortDefinition& cohort
);
[[nodiscard]] std::string render_cohort_list(
    const std::vector<application::CohortDefinition>& cohorts
);

struct ParsedCreateCohortRequest final {
    std::string name;
    std::vector<application::CohortMemberDraft> members;
};

struct ParsedReviseCohortRequest final {
    std::uint32_t expected_revision{0U};
    std::vector<application::CohortMemberDraft> members;
};

[[nodiscard]] ParsedCreateCohortRequest parse_create_cohort_request(
    std::string_view body
);
[[nodiscard]] ParsedReviseCohortRequest parse_revise_cohort_request(
    std::string_view body
);

}  // namespace biocore::presentation
