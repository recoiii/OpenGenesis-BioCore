#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "biocore/application/cohort_results.hpp"
#include "biocore/application/i_cohort_analysis_snapshot_store.hpp"

namespace biocore::application {

class CohortResultsService final {
public:
    static constexpr std::size_t maximum_export_rows = 10000U;

    explicit CohortResultsService(ICohortAnalysisSnapshotStore& snapshots) noexcept;

    [[nodiscard]] CohortResultPage query(
        std::string_view project_id,
        std::string_view analysis_id,
        const CohortCaseControlAnalysisResult& analysis,
        const CohortResultQuery& request,
        const std::vector<CohortResultAnnotationLink>& annotations = {}
    );

    [[nodiscard]] CohortResultPackage build_package(
        std::string_view project_id,
        std::string_view analysis_id,
        const CohortCaseControlAnalysisResult& analysis,
        const CohortResultQuery& filter,
        std::string producer_version,
        std::string generated_at_utc,
        const std::vector<CohortResultAnnotationLink>& annotations = {},
        std::size_t maximum_rows = maximum_export_rows
    );

private:
    ICohortAnalysisSnapshotStore& snapshots_;
};

}  // namespace biocore::application
