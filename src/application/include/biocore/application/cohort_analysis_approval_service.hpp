#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

#include "biocore/application/cohort_analysis_snapshot.hpp"

namespace biocore::application {

class ICohortAnalysisDigester;
class ICohortAnalysisSnapshotStore;
class ICohortMatrixBuilder;
class ICohortRegistryStore;
class IIdGenerator;
class IManagedFileRepository;
class IResultArtifactReader;
class IUtcClock;

enum class CohortAnalysisApprovalErrorCode {
    invalid_request,
    preview_not_ready,
    stale_preview,
    analysis_id_exhausted,
    persistence_conflict
};

class CohortAnalysisApprovalError final : public std::runtime_error {
public:
    CohortAnalysisApprovalError(
        CohortAnalysisApprovalErrorCode code,
        std::string message
    );
    [[nodiscard]] CohortAnalysisApprovalErrorCode code() const noexcept;
private:
    CohortAnalysisApprovalErrorCode code_;
};

class CohortAnalysisApprovalService final {
public:
    static constexpr std::size_t maximum_id_generation_attempts = 8U;
    static constexpr std::size_t maximum_qc_summary_bytes = 4U * 1024U * 1024U;
    static constexpr std::size_t maximum_fisher_table_states = 100000U;

    CohortAnalysisApprovalService(
        ICohortMatrixBuilder& matrix_builder,
        ICohortRegistryStore& cohorts,
        IManagedFileRepository& files,
        IResultArtifactReader& artifact_reader,
        ICohortAnalysisSnapshotStore& snapshots,
        ICohortAnalysisDigester& digester,
        IIdGenerator& ids,
        IUtcClock& clock
    ) noexcept;

    [[nodiscard]] CohortAnalysisApprovalPreview preview(
        const CohortAnalysisApprovalRequest& request
    );

    [[nodiscard]] CohortAnalysisSnapshot approve(
        const ApproveCohortAnalysisRequest& request
    );

private:
    ICohortMatrixBuilder& matrix_builder_;
    ICohortRegistryStore& cohorts_;
    IManagedFileRepository& files_;
    IResultArtifactReader& artifact_reader_;
    ICohortAnalysisSnapshotStore& snapshots_;
    ICohortAnalysisDigester& digester_;
    IIdGenerator& ids_;
    IUtcClock& clock_;
};

}  // namespace biocore::application
