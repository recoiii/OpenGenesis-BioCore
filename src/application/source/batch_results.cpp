#include "biocore/application/batch_results.hpp"

namespace biocore::application {

std::string_view to_string(const BatchResultSampleState state) noexcept {
    switch (state) {
        case BatchResultSampleState::excluded: return "excluded";
        case BatchResultSampleState::not_submitted: return "not_submitted";
        case BatchResultSampleState::submitted: return "submitted";
    }
    return "not_submitted";
}

std::string_view to_string(const BatchQcComparisonState state) noexcept {
    switch (state) {
        case BatchQcComparisonState::comparable: return "comparable";
        case BatchQcComparisonState::incomplete: return "incomplete";
        case BatchQcComparisonState::incompatible: return "incompatible";
    }
    return "incomplete";
}

}  // namespace biocore::application
