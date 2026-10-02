#pragma once

#include "biocore/application/cohort_matrix.hpp"
#include "biocore/application/i_cohort_matrix_builder.hpp"

namespace biocore::application {

class CohortAnalysisSelectionService;
class IManagedFileRepository;
class IReferenceGenomeReader;
class IResultArtifactReader;

class CohortMatrixService final : public ICohortMatrixBuilder {
public:
    CohortMatrixService(
        CohortAnalysisSelectionService& selection_service,
        IManagedFileRepository& files,
        IResultArtifactReader& artifact_reader,
        IReferenceGenomeReader& reference_reader
    ) noexcept;

    [[nodiscard]] CohortMatrixBuildResult build(
        const CohortAnalysisSelectionRequest& request
    ) override;

private:
    CohortAnalysisSelectionService& selection_service_;
    IManagedFileRepository& files_;
    IResultArtifactReader& artifact_reader_;
    IReferenceGenomeReader& reference_reader_;
};

}  // namespace biocore::application
