#pragma once

#include "biocore/application/i_cohort_analysis_digester.hpp"

namespace biocore::infrastructure {

class Sha256CohortAnalysisDigester final
    : public application::ICohortAnalysisDigester {
public:
    [[nodiscard]] std::string sha256(std::string_view canonical_bytes) override;
};

}  // namespace biocore::infrastructure
