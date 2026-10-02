#include "biocore/infrastructure/sha256_cohort_analysis_digester.hpp"

#include <cstddef>
#include <span>
#include <string_view>

#include "biocore/infrastructure/sha256.hpp"

namespace biocore::infrastructure {

std::string Sha256CohortAnalysisDigester::sha256(
    const std::string_view canonical_bytes
) {
    const auto* data = reinterpret_cast<const std::byte*>(canonical_bytes.data());
    return sha256_hex(std::span<const std::byte>{data, canonical_bytes.size()});
}

}  // namespace biocore::infrastructure
