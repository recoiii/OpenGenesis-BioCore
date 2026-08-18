#include "biocore/application/worker_event_ingestion_error.hpp"

#include <utility>

namespace biocore::application {

WorkerEventIngestionError::WorkerEventIngestionError(
    const WorkerEventIngestionErrorCode code,
    std::string job_id,
    std::string message
)
    : std::runtime_error{std::move(message)}, code_{code}, job_id_{std::move(job_id)} {}

WorkerEventIngestionErrorCode WorkerEventIngestionError::code() const noexcept {
    return code_;
}

std::string_view WorkerEventIngestionError::job_id() const noexcept {
    return job_id_;
}

}  // namespace biocore::application
