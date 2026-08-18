#include "biocore/application/worker_runtime_error.hpp"

#include <utility>

namespace biocore::application {

WorkerRuntimeError::WorkerRuntimeError(
    const WorkerRuntimeErrorCode code,
    std::string message
)
    : std::runtime_error{std::move(message)}, code_{code} {}

WorkerRuntimeErrorCode WorkerRuntimeError::code() const noexcept { return code_; }

}  // namespace biocore::application
