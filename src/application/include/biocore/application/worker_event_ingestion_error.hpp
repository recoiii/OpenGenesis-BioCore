#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace biocore::application {

enum class WorkerEventIngestionErrorCode {
    identity_mismatch,
    sequence_mismatch,
    lifecycle_violation,
    invalid_event,
    concurrent_update
};

class WorkerEventIngestionError final : public std::runtime_error {
public:
    WorkerEventIngestionError(
        WorkerEventIngestionErrorCode code,
        std::string job_id,
        std::string message
    );

    [[nodiscard]] WorkerEventIngestionErrorCode code() const noexcept;
    [[nodiscard]] std::string_view job_id() const noexcept;

private:
    WorkerEventIngestionErrorCode code_;
    std::string job_id_;
};

}  // namespace biocore::application
