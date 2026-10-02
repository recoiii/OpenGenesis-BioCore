#pragma once

#include <cstddef>

#include "biocore/application/cohort_analysis_selection.hpp"

namespace biocore::application {

class IBatchExecutionStore;
class IBatchPlanStore;
class ICohortRegistryStore;
class IInputFileStorage;
class IJobRepository;
class IManagedFileRepository;
class IReferenceManifestReader;
class IResultArtifactReader;

class CohortAnalysisSelectionService final {
public:
    static constexpr std::size_t maximum_samples = 100U;
    static constexpr std::size_t maximum_sources = 100U;
    static constexpr std::size_t maximum_vcf_bytes = 64U * 1024U * 1024U;
    static constexpr std::size_t maximum_combined_vcf_bytes = 256U * 1024U * 1024U;
    static constexpr std::size_t maximum_reference_bytes = 512U * 1024U * 1024U;
    static constexpr const char* normalization_contract_v1 = "biocore.normalized-allele.v1";

    CohortAnalysisSelectionService(
        ICohortRegistryStore& cohorts,
        IBatchPlanStore& plans,
        IBatchExecutionStore& executions,
        IJobRepository& jobs,
        IManagedFileRepository& files,
        IInputFileStorage& input_storage,
        IResultArtifactReader& artifact_reader,
        IReferenceManifestReader& reference_reader
    ) noexcept;

    [[nodiscard]] CohortAnalysisSelectionPreview preview(
        const CohortAnalysisSelectionRequest& request
    );

private:
    ICohortRegistryStore& cohorts_;
    IBatchPlanStore& plans_;
    IBatchExecutionStore& executions_;
    IJobRepository& jobs_;
    IManagedFileRepository& files_;
    IInputFileStorage& input_storage_;
    IResultArtifactReader& artifact_reader_;
    IReferenceManifestReader& reference_reader_;
};

}  // namespace biocore::application
