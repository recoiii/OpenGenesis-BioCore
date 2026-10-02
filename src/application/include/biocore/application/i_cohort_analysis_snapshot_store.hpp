#pragma once

#include <optional>
#include <string_view>

#include "biocore/application/cohort_analysis_snapshot.hpp"

namespace biocore::application {

enum class CohortAnalysisStoreResult {
    stored,
    analysis_id_conflict,
    project_not_found,
    cohort_revision_not_found,
    source_not_found
};

class ICohortAnalysisSnapshotStore {
public:
    virtual ~ICohortAnalysisSnapshotStore() = default;

    virtual CohortAnalysisStoreResult create(
        const CohortAnalysisSnapshot& snapshot
    ) = 0;

    [[nodiscard]] virtual std::optional<CohortAnalysisSnapshot> find(
        std::string_view project_id,
        std::string_view analysis_id
    ) = 0;
};

}  // namespace biocore::application
