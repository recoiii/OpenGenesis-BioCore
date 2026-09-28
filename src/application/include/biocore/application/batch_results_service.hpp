#pragma once

#include <cstddef>
#include <string_view>

#include "biocore/application/batch_results.hpp"
#include "biocore/domain/reference_database.hpp"
#include "biocore/domain/reference_genome.hpp"

namespace biocore::application {

class IBatchExecutionStore;
class IBatchPlanStore;
class IJobRepository;
class IManagedFileRepository;
class IResultArtifactReader;

class BatchResultsService final {
public:
    static constexpr std::size_t maximum_qc_summary_bytes = 4U * 1024U * 1024U;
    static constexpr std::size_t maximum_matrix_vcf_bytes = 512U * 1024U * 1024U;

    BatchResultsService(
        IBatchPlanStore& plans,
        IBatchExecutionStore& executions,
        IJobRepository& jobs,
        IManagedFileRepository& managed_files,
        IResultArtifactReader& artifact_reader
    ) noexcept;

    [[nodiscard]] BatchResultsOverview overview(std::string_view plan_id);

    [[nodiscard]] BatchVariantMatrixPreview preview_variant_matrix(
        std::string_view plan_id
    );

    [[nodiscard]] BatchVariantMatrixBuild build_variant_matrix(
        std::string_view plan_id,
        const domain::ReferenceAssemblyIdentity& assembly,
        const domain::ReferenceGenome& reference
    );

private:
    IBatchPlanStore& plans_;
    IBatchExecutionStore& executions_;
    IJobRepository& jobs_;
    IManagedFileRepository& managed_files_;
    IResultArtifactReader& artifact_reader_;
};

}  // namespace biocore::application
