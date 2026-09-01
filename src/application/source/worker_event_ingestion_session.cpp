#include "biocore/application/worker_event_ingestion_session.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "biocore/application/job_service.hpp"
#include "biocore/application/job_service_error.hpp"
#include "biocore/application/output_artifact_cleanup_service.hpp"
#include "biocore/application/output_artifact_service.hpp"
#include "biocore/application/worker_event_ingestion_error.hpp"
#include "biocore/domain/job_status.hpp"

namespace biocore::application {
namespace {

[[nodiscard]] bool is_blank(const std::string_view value) {
    return value.empty() || std::ranges::all_of(value, [](const char character) {
               return std::isspace(static_cast<unsigned char>(character)) != 0;
           });
}

void require_text(const std::string_view value, const std::string_view field) {
    if (is_blank(value) || value.find('\0') != std::string_view::npos) {
        throw WorkerEventIngestionError{
            WorkerEventIngestionErrorCode::invalid_event,
            {},
            std::string{field} + " is invalid",
        };
    }
}

void require_bounded_text(
    const std::string_view value,
    const std::string_view field,
    const std::size_t maximum_length,
    const std::string& job_id
) {
    if (is_blank(value) || value.find('\0') != std::string_view::npos ||
        value.size() > maximum_length) {
        throw WorkerEventIngestionError{
            WorkerEventIngestionErrorCode::invalid_event,
            job_id,
            std::string{field} + " is invalid",
        };
    }
}

void validate_event_shape(const WorkerLifecycleEvent& event) {
    require_bounded_text(event.job_id, "Worker job id", 128U, event.job_id);
    require_bounded_text(
        event.worker_timestamp_utc, "Worker event timestamp", 200U, event.job_id
    );
    if (event.launch_revision < 0 || event.sequence == 0U) {
        throw WorkerEventIngestionError{
            WorkerEventIngestionErrorCode::invalid_event,
            event.job_id,
            "Worker event revision or sequence is invalid",
        };
    }

    const auto has_artifact_payload = [&] {
        return event.artifact_step_id.has_value() || event.artifact_output_port.has_value() ||
               event.artifact_plugin_id.has_value() || event.artifact_plugin_version.has_value() ||
               event.artifact_module_id.has_value() || event.artifact_file_type.has_value() ||
               event.artifact_relative_project_path.has_value();
    };
    const auto has_base_payload = [&] {
        return event.progress.has_value() || event.active_step_id.has_value() ||
               event.log_level.has_value() || event.component.has_value() ||
               event.message.has_value() || event.exit_code.has_value() || has_artifact_payload();
    };

    switch (event.type) {
        case WorkerLifecycleEventType::ready:
        case WorkerLifecycleEventType::heartbeat:
            if (has_base_payload()) {
                throw WorkerEventIngestionError{
                    WorkerEventIngestionErrorCode::invalid_event,
                    event.job_id,
                    "Worker base lifecycle event contains unrelated fields",
                };
            }
            return;
        case WorkerLifecycleEventType::progress:
            if (!event.progress.has_value() || !std::isfinite(*event.progress) ||
                *event.progress < 0.0 || *event.progress > 1.0 ||
                event.log_level.has_value() || event.component.has_value() ||
                event.message.has_value() || event.exit_code.has_value() || has_artifact_payload()) {
                throw WorkerEventIngestionError{
                    WorkerEventIngestionErrorCode::invalid_event,
                    event.job_id,
                    "Worker progress event is invalid",
                };
            }
            if (event.active_step_id.has_value()) {
                require_bounded_text(
                    *event.active_step_id, "Worker active step id", 200U, event.job_id
                );
            }
            return;
        case WorkerLifecycleEventType::log:
            if (!event.log_level.has_value() || !event.component.has_value() ||
                !event.message.has_value() || event.progress.has_value() ||
                event.active_step_id.has_value() || event.exit_code.has_value() ||
                has_artifact_payload()) {
                throw WorkerEventIngestionError{
                    WorkerEventIngestionErrorCode::invalid_event,
                    event.job_id,
                    "Worker log event is invalid",
                };
            }
            require_bounded_text(
                *event.component, "Worker log component", 200U, event.job_id
            );
            require_bounded_text(
                *event.message, "Worker log message", 16U * 1024U, event.job_id
            );
            return;
        case WorkerLifecycleEventType::artifact:
            if (!event.artifact_step_id.has_value() ||
                !event.artifact_output_port.has_value() ||
                !event.artifact_plugin_id.has_value() ||
                !event.artifact_plugin_version.has_value() ||
                !event.artifact_module_id.has_value() ||
                !event.artifact_file_type.has_value() ||
                !event.artifact_relative_project_path.has_value() ||
                event.progress.has_value() || event.active_step_id.has_value() ||
                event.log_level.has_value() || event.component.has_value() ||
                event.message.has_value() || event.exit_code.has_value()) {
                throw WorkerEventIngestionError{
                    WorkerEventIngestionErrorCode::invalid_event,
                    event.job_id,
                    "Worker artifact event is invalid",
                };
            }
            require_bounded_text(*event.artifact_step_id, "Artifact step id", 200U, event.job_id);
            require_bounded_text(*event.artifact_output_port, "Artifact output port", 200U, event.job_id);
            require_bounded_text(*event.artifact_plugin_id, "Artifact plugin id", 200U, event.job_id);
            require_bounded_text(*event.artifact_plugin_version, "Artifact plugin version", 200U, event.job_id);
            require_bounded_text(*event.artifact_module_id, "Artifact module id", 200U, event.job_id);
            require_bounded_text(*event.artifact_file_type, "Artifact file type", 128U, event.job_id);
            require_bounded_text(
                *event.artifact_relative_project_path,
                "Artifact relative project path",
                4096U,
                event.job_id
            );
            return;
        case WorkerLifecycleEventType::completed:
            if (!event.exit_code.has_value() || *event.exit_code != 0 ||
                event.progress.has_value() || event.active_step_id.has_value() ||
                event.log_level.has_value() || event.component.has_value() ||
                event.message.has_value() || has_artifact_payload()) {
                throw WorkerEventIngestionError{
                    WorkerEventIngestionErrorCode::invalid_event,
                    event.job_id,
                    "Worker completed event is invalid",
                };
            }
            return;
        case WorkerLifecycleEventType::failed:
            if (!event.exit_code.has_value() || *event.exit_code == 0 ||
                !event.message.has_value() || event.progress.has_value() ||
                event.active_step_id.has_value() || event.log_level.has_value() ||
                event.component.has_value() || has_artifact_payload()) {
                throw WorkerEventIngestionError{
                    WorkerEventIngestionErrorCode::invalid_event,
                    event.job_id,
                    "Worker failed event is invalid",
                };
            }
            require_bounded_text(
                *event.message, "Worker failure message", 16U * 1024U, event.job_id
            );
            return;
    }
}

[[nodiscard]] WorkerEventIngestionError lifecycle_error(
    const std::string& job_id,
    const std::string& message
) {
    return WorkerEventIngestionError{
        WorkerEventIngestionErrorCode::lifecycle_violation, job_id, message
    };
}

}  // namespace

WorkerEventIngestionSession::WorkerEventIngestionSession(
    JobService& job_service,
    std::string expected_job_id,
    const std::int64_t expected_launch_revision,
    OutputArtifactService* const output_artifact_service,
    OutputArtifactCleanupService* const output_artifact_cleanup_service
)
    : job_service_{job_service},
      output_artifact_service_{output_artifact_service},
      output_artifact_cleanup_service_{output_artifact_cleanup_service},
      expected_job_id_{std::move(expected_job_id)},
      expected_launch_revision_{expected_launch_revision} {
    require_text(expected_job_id_, "Expected worker job id");
    if (expected_launch_revision_ < 0) {
        throw std::invalid_argument("Expected worker launch revision must not be negative");
    }
}

WorkerEventIngestionResult WorkerEventIngestionSession::ingest(
    const WorkerLifecycleEvent& event
) {
    std::scoped_lock lock{mutex_};
    validate_identity_and_sequence(event);
    if (terminal_received_) {
        throw lifecycle_error(expected_job_id_, "Worker event arrived after a terminal event");
    }

    WorkerEventIngestionResult result;
    try {
        switch (event.type) {
            case WorkerLifecycleEventType::ready: {
                if (ready_received_) {
                    throw lifecycle_error(expected_job_id_, "Worker ready event was duplicated");
                }
                const domain::Job current = require_current_job();
                if (current.status() != domain::JobStatus::preparing ||
                    current.revision() != expected_launch_revision_) {
                    throw lifecycle_error(
                        expected_job_id_, "Worker ready event does not match the preparing job"
                    );
                }
                result.persisted_job = job_service_.transition(
                    expected_job_id_, domain::JobStatus::running, current.progress(),
                    current.active_step_id()
                );
                ready_received_ = true;
                result.action = WorkerEventIngestionAction::job_started;
                break;
            }
            case WorkerLifecycleEventType::heartbeat: {
                if (!ready_received_) {
                    throw lifecycle_error(expected_job_id_, "Heartbeat arrived before ready");
                }
                const domain::Job current = require_current_job();
                if (!domain::occupies_worker_slot(current.status())) {
                    throw lifecycle_error(expected_job_id_, "Heartbeat arrived for an inactive job");
                }
                result.action = WorkerEventIngestionAction::heartbeat_observed;
                break;
            }
            case WorkerLifecycleEventType::progress: {
                if (!ready_received_ || !event.progress.has_value()) {
                    throw lifecycle_error(expected_job_id_, "Progress arrived before ready or lacks a value");
                }
                const domain::Job current = require_current_job();
                if (current.status() != domain::JobStatus::running ||
                    *event.progress < current.progress()) {
                    throw lifecycle_error(expected_job_id_, "Worker progress is stale or job is not running");
                }
                if (!pending_artifacts_.empty()) {
                    if (!event.active_step_id.has_value() ||
                        *event.active_step_id != pending_artifacts_.front().step_id) {
                        throw lifecycle_error(
                            expected_job_id_,
                            "Worker progress does not close the pending artifact step"
                        );
                    }
                    if (output_artifact_service_ == nullptr) {
                        throw lifecycle_error(
                            expected_job_id_, "Artifact registration became unavailable"
                        );
                    }
                    result.registered_artifacts =
                        output_artifact_service_->register_generated_outputs_batch(
                            pending_artifacts_, *event.progress
                        );
                    pending_artifacts_.clear();
                }
                result.persisted_job = job_service_.update_progress(
                    expected_job_id_, *event.progress, event.active_step_id
                );
                result.action = WorkerEventIngestionAction::progress_persisted;
                break;
            }
            case WorkerLifecycleEventType::log:
                if (!event.log_level.has_value() || !event.component.has_value() ||
                    !event.message.has_value()) {
                    throw WorkerEventIngestionError{
                        WorkerEventIngestionErrorCode::invalid_event,
                        expected_job_id_,
                        "Worker log event is incomplete",
                    };
                }
                require_text(*event.component, "Worker log component");
                require_text(*event.message, "Worker log message");
                result.action = WorkerEventIngestionAction::log_observed;
                break;
            case WorkerLifecycleEventType::artifact: {
                if (!ready_received_ || output_artifact_service_ == nullptr) {
                    throw lifecycle_error(
                        expected_job_id_,
                        "Artifact event arrived before ready or artifact registration is unavailable"
                    );
                }
                const domain::Job current = require_current_job();
                if (current.status() != domain::JobStatus::running) {
                    throw lifecycle_error(expected_job_id_, "Artifact event requires a running job");
                }
                buffer_artifact(event);
                result.action = WorkerEventIngestionAction::artifact_buffered;
                break;
            }
            case WorkerLifecycleEventType::completed: {
                if (!ready_received_ || !event.exit_code.has_value() || *event.exit_code != 0) {
                    throw lifecycle_error(expected_job_id_, "Completed event is invalid or arrived before ready");
                }
                const domain::Job current = require_current_job();
                if (current.status() != domain::JobStatus::running) {
                    throw lifecycle_error(expected_job_id_, "Completed event requires a running job");
                }
                if (!pending_artifacts_.empty()) {
                    throw lifecycle_error(
                        expected_job_id_, "Completed event arrived with an uncommitted artifact batch"
                    );
                }
                result.persisted_job = job_service_.transition(
                    expected_job_id_, domain::JobStatus::completed, 1.0, std::nullopt
                );
                terminal_received_ = true;
                terminal_exit_code_ = 0;
                result.action = WorkerEventIngestionAction::job_completed;
                break;
            }
            case WorkerLifecycleEventType::failed: {
                if (!event.exit_code.has_value() || *event.exit_code == 0 ||
                    !event.message.has_value()) {
                    throw WorkerEventIngestionError{
                        WorkerEventIngestionErrorCode::invalid_event,
                        expected_job_id_,
                        "Worker failed event is incomplete",
                    };
                }
                require_text(*event.message, "Worker failure message");
                const domain::Job current = require_current_job();
                if (current.status() != domain::JobStatus::preparing &&
                    current.status() != domain::JobStatus::running &&
                    current.status() != domain::JobStatus::cancelling) {
                    throw lifecycle_error(expected_job_id_, "Failed event is invalid for the current job state");
                }
                result.persisted_job = job_service_.transition(
                    expected_job_id_,
                    domain::JobStatus::failed,
                    current.progress(),
                    std::nullopt,
                    JobFailureContext{
                        .kind = domain::JobFailureKind::worker_reported_failure,
                        .message = *event.message,
                        .exit_code = event.exit_code,
                        .worker_timestamp_utc = event.worker_timestamp_utc,
                    }
                );
                terminal_received_ = true;
                terminal_exit_code_ = *event.exit_code;
                pending_artifacts_.clear();
                attach_cleanup_observation(result);
                result.action = WorkerEventIngestionAction::job_failed;
                break;
            }
        }
    } catch (const JobServiceError& error) {
        if (error.code() == JobServiceErrorCode::concurrent_update) {
            throw WorkerEventIngestionError{
                WorkerEventIngestionErrorCode::concurrent_update,
                expected_job_id_,
                "Worker event could not be persisted because the job changed concurrently",
            };
        }
        throw;
    }

    last_sequence_ = event.sequence;
    return result;
}

WorkerProcessFinalizationResult WorkerEventIngestionSession::finalize_process_exit(
    const std::int64_t exit_code
) {
    std::scoped_lock lock{mutex_};
    if (process_finalized_) {
        throw lifecycle_error(expected_job_id_, "Worker process exit was already finalized");
    }
    if (exit_code < 0) {
        throw WorkerEventIngestionError{
            WorkerEventIngestionErrorCode::invalid_event,
            expected_job_id_,
            "Worker process exit code must not be negative",
        };
    }

    if (terminal_received_) {
        if (!terminal_exit_code_.has_value() || *terminal_exit_code_ != exit_code) {
            throw lifecycle_error(
                expected_job_id_, "Worker process exit code conflicts with the terminal event"
            );
        }
        process_finalized_ = true;
        return WorkerProcessFinalizationResult{
            .matched_terminal_event = true,
            .persisted_job = std::nullopt,
            .cleanup_result = std::nullopt,
            .cleanup_error = std::nullopt,
        };
    }

    const domain::Job current = require_current_job();
    if (!domain::occupies_worker_slot(current.status())) {
        throw lifecycle_error(
            expected_job_id_, "Worker process exited while the job did not occupy a worker slot"
        );
    }
    const domain::JobStatus target = current.status() == domain::JobStatus::cancelling
                                         ? domain::JobStatus::cancelled
                                         : domain::JobStatus::interrupted;
    auto finalized = current.status() == domain::JobStatus::cancelling
                         ? job_service_.transition(
                               expected_job_id_, target, current.progress(), std::nullopt
                           )
                         : job_service_.transition(
                               expected_job_id_,
                               target,
                               current.progress(),
                               std::nullopt,
                               JobFailureContext{
                                   .kind = domain::JobFailureKind::process_exit_without_terminal,
                                   .message = "Worker process exited without a terminal lifecycle event.",
                                   .exit_code = exit_code,
                                   .worker_timestamp_utc = std::nullopt,
                               }
                           );
    terminal_received_ = true;
    process_finalized_ = true;
    terminal_exit_code_ = exit_code;
    pending_artifacts_.clear();
    WorkerProcessFinalizationResult result{
        .matched_terminal_event = false,
        .persisted_job = std::move(finalized),
        .cleanup_result = std::nullopt,
        .cleanup_error = std::nullopt,
    };
    attach_cleanup_observation(result);
    return result;
}

void WorkerEventIngestionSession::buffer_artifact(const WorkerLifecycleEvent& event) {
    RegisterGeneratedOutputRequest request{
        .job_id = expected_job_id_,
        .step_id = *event.artifact_step_id,
        .output_port = *event.artifact_output_port,
        .plugin_id = *event.artifact_plugin_id,
        .plugin_version = *event.artifact_plugin_version,
        .module_id = *event.artifact_module_id,
        .file_type = *event.artifact_file_type,
        .relative_project_path = *event.artifact_relative_project_path,
    };
    if (!pending_artifacts_.empty()) {
        const auto& first = pending_artifacts_.front();
        if (request.step_id != first.step_id || request.plugin_id != first.plugin_id ||
            request.plugin_version != first.plugin_version || request.module_id != first.module_id) {
            throw lifecycle_error(
                expected_job_id_, "Artifact events from different steps or modules were interleaved"
            );
        }
        if (std::ranges::any_of(pending_artifacts_, [&request](const auto& pending) {
                return pending.output_port == request.output_port ||
                       pending.relative_project_path == request.relative_project_path;
            })) {
            throw lifecycle_error(expected_job_id_, "Artifact event was duplicated within a step");
        }
    }
    pending_artifacts_.push_back(std::move(request));
}

void WorkerEventIngestionSession::attach_cleanup_observation(
    WorkerEventIngestionResult& result
) noexcept {
    if (output_artifact_cleanup_service_ == nullptr) return;
    try {
        result.cleanup_result =
            output_artifact_cleanup_service_->quarantine_unregistered_for_job(expected_job_id_);
    } catch (const std::exception& error) {
        result.cleanup_error = error.what();
    } catch (...) {
        result.cleanup_error = "Unknown partial-output cleanup failure";
    }
}

void WorkerEventIngestionSession::attach_cleanup_observation(
    WorkerProcessFinalizationResult& result
) noexcept {
    if (output_artifact_cleanup_service_ == nullptr) return;
    try {
        result.cleanup_result =
            output_artifact_cleanup_service_->quarantine_unregistered_for_job(expected_job_id_);
    } catch (const std::exception& error) {
        result.cleanup_error = error.what();
    } catch (...) {
        result.cleanup_error = "Unknown partial-output cleanup failure";
    }
}

std::uint64_t WorkerEventIngestionSession::last_sequence() const {
    std::scoped_lock lock{mutex_};
    return last_sequence_;
}

bool WorkerEventIngestionSession::ready_received() const {
    std::scoped_lock lock{mutex_};
    return ready_received_;
}

bool WorkerEventIngestionSession::terminal_received() const {
    std::scoped_lock lock{mutex_};
    return terminal_received_;
}

std::size_t WorkerEventIngestionSession::pending_artifact_count() const {
    std::scoped_lock lock{mutex_};
    return pending_artifacts_.size();
}

void WorkerEventIngestionSession::validate_identity_and_sequence(
    const WorkerLifecycleEvent& event
) const {
    validate_event_shape(event);
    if (event.job_id != expected_job_id_ || event.launch_revision != expected_launch_revision_) {
        throw WorkerEventIngestionError{
            WorkerEventIngestionErrorCode::identity_mismatch,
            event.job_id,
            "Worker event identity or launch revision does not match the session",
        };
    }
    require_text(event.worker_timestamp_utc, "Worker event timestamp");
    if (last_sequence_ == std::numeric_limits<std::uint64_t>::max() ||
        event.sequence != last_sequence_ + 1U) {
        throw WorkerEventIngestionError{
            WorkerEventIngestionErrorCode::sequence_mismatch,
            event.job_id,
            "Worker event sequence is missing, duplicated, or out of order",
        };
    }
}

domain::Job WorkerEventIngestionSession::require_current_job() const {
    const auto job = job_service_.find_by_id(expected_job_id_);
    if (!job.has_value()) {
        throw WorkerEventIngestionError{
            WorkerEventIngestionErrorCode::lifecycle_violation,
            expected_job_id_,
            "Worker event references a job that no longer exists",
        };
    }
    return *job;
}

}  // namespace biocore::application
