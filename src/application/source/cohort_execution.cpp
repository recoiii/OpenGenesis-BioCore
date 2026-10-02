#include "biocore/application/cohort_execution.hpp"

namespace biocore::application {

std::string_view to_string(const CohortExecutionState state) noexcept {
    switch (state) {
        case CohortExecutionState::queued: return "queued";
        case CohortExecutionState::running: return "running";
        case CohortExecutionState::completed: return "completed";
        case CohortExecutionState::failed: return "failed";
        case CohortExecutionState::cancelled: return "cancelled";
        case CohortExecutionState::interrupted: return "interrupted";
    }
    return "interrupted";
}

std::optional<CohortExecutionState>
cohort_execution_state_from_string(const std::string_view value) noexcept {
    if (value == "queued") return CohortExecutionState::queued;
    if (value == "running") return CohortExecutionState::running;
    if (value == "completed") return CohortExecutionState::completed;
    if (value == "failed") return CohortExecutionState::failed;
    if (value == "cancelled") return CohortExecutionState::cancelled;
    if (value == "interrupted") return CohortExecutionState::interrupted;
    return std::nullopt;
}

bool is_terminal(const CohortExecutionState state) noexcept {
    return state == CohortExecutionState::completed ||
           state == CohortExecutionState::failed ||
           state == CohortExecutionState::cancelled ||
           state == CohortExecutionState::interrupted;
}

bool is_retryable(const CohortExecutionState state) noexcept {
    return state == CohortExecutionState::failed ||
           state == CohortExecutionState::cancelled ||
           state == CohortExecutionState::interrupted;
}

}  // namespace biocore::application
