#pragma once

#include <string>

#include "biocore/application/cohort_results.hpp"

namespace biocore::presentation {

[[nodiscard]] std::string render_cohort_result_package_json(
    const application::CohortResultPackage& package
);

[[nodiscard]] std::string render_cohort_result_package_csv(
    const application::CohortResultPackage& package
);

[[nodiscard]] std::string render_cohort_result_package_tsv(
    const application::CohortResultPackage& package
);

[[nodiscard]] std::string render_cohort_result_package_html(
    const application::CohortResultPackage& package
);

}  // namespace biocore::presentation
