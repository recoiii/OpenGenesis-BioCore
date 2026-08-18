#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace biocore::application {

enum class WorkerRuntimeErrorCode {
    invalid_policy,
    already_running,
    cycle_already_in_progress
};

class WorkerRuntimeError final : public std::runtime_error {
public:
    WorkerRuntimeError(WorkerRuntimeErrorCode code, std::string message);
    [[nodiscard]] WorkerRuntimeErrorCode code() const noexcept;

private:
    WorkerRuntimeErrorCode code_;
};

}  // namespace biocore::application
