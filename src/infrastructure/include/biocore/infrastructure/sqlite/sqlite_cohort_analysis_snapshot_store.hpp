#pragma once

#include "biocore/application/i_cohort_analysis_snapshot_store.hpp"

namespace biocore::infrastructure::sqlite {

class SqliteConnection;

class SqliteCohortAnalysisSnapshotStore final
    : public application::ICohortAnalysisSnapshotStore {
public:
    explicit SqliteCohortAnalysisSnapshotStore(SqliteConnection& connection) noexcept;

    application::CohortAnalysisStoreResult create(
        const application::CohortAnalysisSnapshot& snapshot
    ) override;

    [[nodiscard]] std::optional<application::CohortAnalysisSnapshot> find(
        std::string_view project_id,
        std::string_view analysis_id
    ) override;

private:
    SqliteConnection& connection_;
};

}  // namespace biocore::infrastructure::sqlite
