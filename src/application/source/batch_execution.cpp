#include "biocore/application/batch_execution.hpp"

namespace biocore::application {

std::string_view to_string(const BatchExecutionState state) noexcept {
    switch (state) {
        case BatchExecutionState::active: return "active";
        case BatchExecutionState::attention: return "attention";
        case BatchExecutionState::cancelling: return "cancelling";
        case BatchExecutionState::cancelled: return "cancelled";
        case BatchExecutionState::completed: return "completed";
        case BatchExecutionState::failed: return "failed";
        case BatchExecutionState::partial_failure: return "partial_failure";
    }
    return "attention";
}

}  // namespace biocore::application
