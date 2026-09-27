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

std::string_view to_string(const BatchAttemptMode mode) noexcept {
    switch (mode) {
        case BatchAttemptMode::initial: return "initial";
        case BatchAttemptMode::resume: return "resume";
        case BatchAttemptMode::retry: return "retry";
    }
    return "initial";
}

std::optional<BatchAttemptMode> batch_attempt_mode_from_string(
    const std::string_view value
) noexcept {
    if (value == "initial") return BatchAttemptMode::initial;
    if (value == "resume") return BatchAttemptMode::resume;
    if (value == "retry") return BatchAttemptMode::retry;
    return std::nullopt;
}

}  // namespace biocore::application
