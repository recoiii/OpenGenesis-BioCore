#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "biocore/application/cohort_analysis_selection.hpp"
#include "biocore/domain/multi_sample_matrix.hpp"

namespace biocore::application {

struct CohortMatrixBudget final {
    static constexpr std::size_t maximum_samples = 100U;
    static constexpr std::size_t maximum_sources = 100U;
    static constexpr std::size_t maximum_normalized_alleles = 10000U;
    static constexpr std::size_t maximum_observations = 1000000U;
};

struct CohortMatrixDispatchSource final {
    std::string project_sample_id;
    std::string managed_file_id;
    std::string sha256;
    std::string vcf_sample_name;

    friend bool operator==(
        const CohortMatrixDispatchSource&,
        const CohortMatrixDispatchSource&
    ) = default;
};

struct CohortMatrixDispatchPlan final {
    std::string project_id;
    std::string cohort_id;
    std::uint32_t cohort_revision{0U};
    std::string stage_id{"matrix"};
    std::string native_module_id{"org.biocore.cohort.matrix"};
    std::string normalization_contract_version;
    std::string reference_file_id;
    std::string reference_sha256;
    std::vector<CohortMatrixDispatchSource> sources;
    std::size_t maximum_samples{CohortMatrixBudget::maximum_samples};
    std::size_t maximum_sources{CohortMatrixBudget::maximum_sources};
    std::size_t maximum_normalized_alleles{CohortMatrixBudget::maximum_normalized_alleles};
    std::size_t maximum_observations{CohortMatrixBudget::maximum_observations};

    friend bool operator==(const CohortMatrixDispatchPlan&, const CohortMatrixDispatchPlan&) = default;
};

struct CohortMatrixBuildResult final {
    CohortAnalysisSelectionPreview selection;
    CohortMatrixDispatchPlan dispatch;
    domain::MultiSampleMatrix matrix;
    std::size_t parsed_vcf_artifacts{0U};
};

void validate_cohort_matrix_budget(
    std::size_t sample_count,
    std::size_t source_count,
    std::size_t normalized_allele_count,
    std::size_t observation_count
);

}  // namespace biocore::application
