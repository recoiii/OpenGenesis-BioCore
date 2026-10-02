#include "biocore/application/cohort_matrix.hpp"

#include <stdexcept>

namespace biocore::application {

void validate_cohort_matrix_budget(
    const std::size_t sample_count,
    const std::size_t source_count,
    const std::size_t normalized_allele_count,
    const std::size_t observation_count
) {
    if (sample_count > CohortMatrixBudget::maximum_samples) {
        throw std::length_error{"Cohort matrix sample limit exceeded"};
    }
    if (source_count > CohortMatrixBudget::maximum_sources) {
        throw std::length_error{"Cohort matrix source limit exceeded"};
    }
    if (normalized_allele_count > CohortMatrixBudget::maximum_normalized_alleles) {
        throw std::length_error{"Cohort matrix normalized-allele limit exceeded"};
    }
    if (observation_count > CohortMatrixBudget::maximum_observations) {
        throw std::length_error{"Cohort matrix observation limit exceeded"};
    }
}

}  // namespace biocore::application
