#pragma once

#include "biocore/application/cohort_matrix.hpp"

namespace biocore::application {

class ICohortMatrixBuilder {
public:
    virtual ~ICohortMatrixBuilder() = default;

    [[nodiscard]] virtual CohortMatrixBuildResult build(
        const CohortAnalysisSelectionRequest& request
    ) = 0;
};

}  // namespace biocore::application
