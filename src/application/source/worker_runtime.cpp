#include "biocore/application/worker_runtime.hpp"

#include <algorithm>
#include <exception>
#include <iterator>
#include <ranges>
#include <stdexcept>
#include <utility>

#include "biocore/application/i_monotonic_clock.hpp"
#include "biocore/application/i_worker_lifecycle_event_sink.hpp"
#include "biocore/application/i_worker_supervisor.hpp"
#include "biocore/application/job_service.hpp"
#include "biocore/application/job_service_error.hpp"
#include "biocore/application/output_artifact_cleanup_service.hpp"
#include "biocore/application/output_artifact_service.hpp"
#include "biocore/application/worker_event_ingestion_session.hpp"
#include "biocore/application/worker_runtime_error.hpp"
#include "biocore/domain/job_status.hpp"

namespace biocore::application {
namespace {

class CycleGuard final {
public:
    explicit CycleGuard(std::atomic_flag& flag) : flag_{flag} {
        if (flag_.test_and_set(std::memory_order_acquire)) {
            throw WorkerRuntimeError{
                WorkerRuntimeErrorCode::cycle_already_in_progress,
                "A worker runtime cycle is already in progress",
            };
        }
    }

    ~CycleGuard() { flag_.clear(std::memory_order_release); }

    CycleGuard(const CycleGuard&) = delete;
    CycleGuard& operator=(const CycleGuard&) = delete;

private:
    std::atomic_flag& flag_;
};

[[nodiscard]] WorkerRuntimeIssue issue(
    const WorkerRuntimeIssueStage stage,
    std::string job_id,
    std::string message
) {
    return WorkerRuntimeIssue{
        .stage = stage,
        .job_id = std::move(job_id),
        .message = std::move(message),
    };
}

template <typename T>
void append_bounded(
    std::vector<T>& target,
    std::vector<T> values,
    const std::size_t limit,
    std::size_t& dropped
) {
    if (values.empty()) return;
    if (values.size() >= limit) {
        dropped += target.size() + values.size() - limit;
        target.assign(
            std::make_move_iterator(values.end() - static_cast<std::ptrdiff_t>(limit)),
            std::make_move_iterator(values.end())
        );
        return;
    }
    const std::size_t required = target.size() + values.size();
    if (required > limit) {
        const std::size_t overflow = required - limit;
        target.erase(target.begin(), target.begin() + static_cast<std::ptrdiff_t>(overflow));
        dropped += overflow;
    }
    target.insert(
        target.end(),
        std::make_move_iterator(values.begin()),
        std::make_move_iterator(values.end())
    );
}

[[nodiscard]] bool is_liveness_event(const WorkerLifecycleEventType type) noexcept {
    // Only explicit control-plane liveness signals extend the heartbeat deadline.
    // Log or artifact spam must not mask a worker that stopped heartbeating.
    return type == WorkerLifecycleEventType::ready ||
           type == WorkerLifecycleEventType::heartbeat ||
           type == WorkerLifecycleEventType::progress;
}

}  // namespace

struct WorkerRuntime::SessionState final {
    SessionState(
        JobService& job_service,
        const WorkerLaunchRequest& launch,
        const std::chrono::steady_clock::time_point launched_at,
        OutputArtifactService* output_artifact_service,
        OutputArtifactCleanupService* output_artifact_cleanup_service
    )
        : ingestion{
              job_service,
              launch.job_id,
              launch.job_revision,
              output_artifact_service,
              output_artifact_cleanup_service
          },
          last_activity{launched_at} {}

    WorkerEventIngestionSession ingestion;
    std::chrono::steady_clock::time_point last_activity;
    bool cancellation_termination_requested{false};
    bool timeout_termination_requested{false};
    bool timeout_persisted_interrupted{false};
};

WorkerRuntime::WorkerRuntime(
    JobScheduler& scheduler,
    JobService& job_service,
    IWorkerSupervisor& worker_supervisor,
    IMonotonicClock& monotonic_clock,
    WorkerRuntimePolicy policy,
    OutputArtifactService* const output_artifact_service,
    OutputArtifactCleanupService* const output_artifact_cleanup_service,
    IWorkerLifecycleEventSink* const lifecycle_event_sink
)
    : scheduler_{scheduler},
      job_service_{job_service},
      worker_supervisor_{worker_supervisor},
      monotonic_clock_{monotonic_clock},
      policy_{policy},
      output_artifact_service_{output_artifact_service},
      output_artifact_cleanup_service_{output_artifact_cleanup_service},
      lifecycle_event_sink_{lifecycle_event_sink} {
    if (policy_.poll_interval <= std::chrono::milliseconds::zero() ||
        policy_.heartbeat_timeout <= std::chrono::milliseconds::zero() ||
        policy_.heartbeat_timeout < policy_.poll_interval ||
        policy_.maximum_background_issues == 0U ||
        policy_.maximum_background_diagnostics == 0U ||
        policy_.maximum_background_protocol_issues == 0U) {
        throw WorkerRuntimeError{
            WorkerRuntimeErrorCode::invalid_policy,
            "Worker runtime intervals and background retention limits must be positive, and heartbeat timeout must not be shorter than poll interval",
        };
    }
}

WorkerRuntime::~WorkerRuntime() { stop(); }

WorkerRuntimeCycleResult WorkerRuntime::run_cycle() {
    const CycleGuard guard{cycle_in_progress_};
    WorkerRuntimeCycleResult result;
    const auto now = monotonic_clock_.now();

    try {
        for (auto& output : worker_supervisor_.poll_output()) {
            ingest_output_batch(std::move(output), now, result);
        }
    } catch (const std::exception& error) {
        result.issues.push_back(issue(WorkerRuntimeIssueStage::output_poll, {}, error.what()));
    } catch (...) {
        result.issues.push_back(issue(
            WorkerRuntimeIssueStage::output_poll, {}, "Unknown worker output polling failure"
        ));
    }

    try {
        for (auto& process_exit : worker_supervisor_.reap_exited()) {
            process_exit_batch(std::move(process_exit), now, result);
        }
    } catch (const std::exception& error) {
        result.issues.push_back(issue(WorkerRuntimeIssueStage::process_reap, {}, error.what()));
    } catch (...) {
        result.issues.push_back(issue(
            WorkerRuntimeIssueStage::process_reap, {}, "Unknown worker process reap failure"
        ));
    }

    process_cancellation_requests(result);
    enforce_heartbeat_timeouts(now, result);

    std::size_t reserved_slots = 0U;
    {
        std::scoped_lock lock{sessions_mutex_};
        reserved_slots = static_cast<std::size_t>(std::ranges::count_if(
            sessions_,
            [](const auto& entry) {
                return entry.second->timeout_persisted_interrupted ||
                       entry.second->ingestion.terminal_received();
            }
        ));
    }

    try {
        result.scheduler = scheduler_.tick(reserved_slots);
        register_launches(result.scheduler, now, result);
    } catch (const std::exception& error) {
        result.issues.push_back(issue(WorkerRuntimeIssueStage::scheduler, {}, error.what()));
    } catch (...) {
        result.issues.push_back(issue(
            WorkerRuntimeIssueStage::scheduler, {}, "Unknown scheduler failure"
        ));
    }

    return result;
}

void WorkerRuntime::start() {
    std::scoped_lock lock{thread_mutex_};
    if (background_thread_.joinable()) {
        throw WorkerRuntimeError{
            WorkerRuntimeErrorCode::already_running,
            "Worker runtime background loop is already running",
        };
    }

    running_.store(true, std::memory_order_release);
    try {
        background_thread_ = std::jthread{
            [this](const std::stop_token stop_token) { background_main(stop_token); }
        };
    } catch (...) {
        running_.store(false, std::memory_order_release);
        throw;
    }
}

void WorkerRuntime::stop() noexcept {
    std::jthread thread;
    {
        std::scoped_lock lock{thread_mutex_};
        if (!background_thread_.joinable()) {
            running_.store(false, std::memory_order_release);
            return;
        }
        background_thread_.request_stop();
        wait_condition_.notify_all();
        thread = std::move(background_thread_);
    }
    if (thread.joinable()) {
        thread.join();
    }
    running_.store(false, std::memory_order_release);
}

bool WorkerRuntime::running() const noexcept {
    return running_.load(std::memory_order_acquire);
}

WorkerRuntimePolicy WorkerRuntime::policy() const noexcept { return policy_; }

std::size_t WorkerRuntime::active_session_count() const {
    std::scoped_lock lock{sessions_mutex_};
    return sessions_.size();
}

std::vector<WorkerRuntimeIssue> WorkerRuntime::drain_background_issues() {
    std::scoped_lock lock{background_issue_mutex_};
    auto result = std::exchange(background_issues_, {});
    const std::size_t dropped = std::exchange(dropped_background_issues_, 0U);
    if (dropped != 0U) {
        result.push_back(issue(
            WorkerRuntimeIssueStage::observation_retention,
            {},
            "Dropped " + std::to_string(dropped) +
                " older background runtime issues because the retention limit was reached"
        ));
    }
    return result;
}

std::vector<std::string> WorkerRuntime::drain_background_diagnostics() {
    std::scoped_lock lock{background_issue_mutex_};
    auto result = std::exchange(background_diagnostics_, {});
    const std::size_t dropped = std::exchange(dropped_background_diagnostics_, 0U);
    if (dropped != 0U) {
        result.push_back(
            "Dropped " + std::to_string(dropped) +
            " older background diagnostics because the retention limit was reached"
        );
    }
    return result;
}

std::vector<WorkerProtocolIssue> WorkerRuntime::drain_background_protocol_issues() {
    std::scoped_lock lock{background_issue_mutex_};
    auto result = std::exchange(background_protocol_issues_, {});
    const std::size_t dropped = std::exchange(dropped_background_protocol_issues_, 0U);
    if (dropped != 0U) {
        result.push_back(WorkerProtocolIssue{
            {},
            "Dropped " + std::to_string(dropped) +
                " older background protocol issues because the retention limit was reached"
        });
    }
    return result;
}

void WorkerRuntime::register_launches(
    const JobSchedulerTickResult& scheduler_result,
    const std::chrono::steady_clock::time_point now,
    WorkerRuntimeCycleResult& result
) {
    std::scoped_lock lock{sessions_mutex_};
    for (const WorkerLaunchRequest& launch : scheduler_result.launched_workers) {
        const auto [iterator, inserted] = sessions_.try_emplace(
            launch.job_id,
            std::make_unique<SessionState>(
                job_service_, launch, now, output_artifact_service_,
                output_artifact_cleanup_service_
            )
        );
        static_cast<void>(iterator);
        if (!inserted) {
            result.issues.push_back(issue(
                WorkerRuntimeIssueStage::event_ingestion,
                launch.job_id,
                "A worker ingestion session is already registered for the launched job"
            ));
        }
    }
}

void WorkerRuntime::ingest_output_batch(
    WorkerProcessOutput output,
    const std::chrono::steady_clock::time_point now,
    WorkerRuntimeCycleResult& result
) {
    for (std::string& diagnostic : output.diagnostics) {
        result.diagnostics.push_back(std::move(diagnostic));
    }
    for (WorkerProtocolIssue& protocol_issue : output.protocol_issues) {
        result.protocol_issues.push_back(std::move(protocol_issue));
    }

    bool ingested_any = false;
    for (const WorkerLifecycleEvent& event : output.events) {
        bool accepted = false;
        {
            std::scoped_lock lock{sessions_mutex_};
            const auto iterator = sessions_.find(output.job_id);
            if (iterator == sessions_.end()) {
                result.issues.push_back(issue(
                    WorkerRuntimeIssueStage::event_ingestion,
                    output.job_id,
                    "Worker output was received without a registered ingestion session"
                ));
                return;
            }

            SessionState& state = *iterator->second;
            if (state.timeout_termination_requested) {
                return;
            }

            try {
                const WorkerEventIngestionResult ingestion_result =
                    state.ingestion.ingest(event);
                if (ingestion_result.cleanup_error.has_value()) {
                    result.issues.push_back(issue(
                        WorkerRuntimeIssueStage::artifact_cleanup,
                        output.job_id,
                        *ingestion_result.cleanup_error
                    ));
                }
                if (is_liveness_event(event.type)) {
                    state.last_activity = now;
                }
                accepted = true;
                ingested_any = true;
            } catch (const std::exception& error) {
                result.issues.push_back(issue(
                    WorkerRuntimeIssueStage::event_ingestion,
                    output.job_id,
                    error.what()
                ));
            } catch (...) {
                result.issues.push_back(issue(
                    WorkerRuntimeIssueStage::event_ingestion,
                    output.job_id,
                    "Unknown worker lifecycle ingestion failure"
                ));
            }
        }

        if (accepted && lifecycle_event_sink_ != nullptr) {
            try {
                lifecycle_event_sink_->publish(event);
            } catch (const std::exception& error) {
                result.issues.push_back(issue(
                    WorkerRuntimeIssueStage::event_broadcast,
                    output.job_id,
                    error.what()
                ));
            } catch (...) {
                result.issues.push_back(issue(
                    WorkerRuntimeIssueStage::event_broadcast,
                    output.job_id,
                    "Unknown worker lifecycle broadcast failure"
                ));
            }
        }
    }

    if (ingested_any) {
        result.ingested_job_ids.push_back(output.job_id);
    }
}

void WorkerRuntime::process_exit_batch(
    WorkerProcessExit process_exit,
    const std::chrono::steady_clock::time_point now,
    WorkerRuntimeCycleResult& result
) {
    WorkerProcessOutput final_output{
        .job_id = process_exit.job_id,
        .process_id = process_exit.process_id,
        .events = std::move(process_exit.events),
        .diagnostics = std::move(process_exit.diagnostics),
        .protocol_issues = std::move(process_exit.protocol_issues),
    };
    ingest_output_batch(std::move(final_output), now, result);

    std::scoped_lock lock{sessions_mutex_};
    const auto iterator = sessions_.find(process_exit.job_id);
    if (iterator == sessions_.end()) {
        result.issues.push_back(issue(
            WorkerRuntimeIssueStage::process_finalization,
            process_exit.job_id,
            "Worker process exit was received without a registered ingestion session"
        ));
        return;
    }

    SessionState& state = *iterator->second;
    if (!state.timeout_persisted_interrupted) {
        try {
            const WorkerProcessFinalizationResult finalization =
                state.ingestion.finalize_process_exit(process_exit.exit_code);
            if (finalization.cleanup_error.has_value()) {
                result.issues.push_back(issue(
                    WorkerRuntimeIssueStage::artifact_cleanup,
                    process_exit.job_id,
                    *finalization.cleanup_error
                ));
            }
            if (finalization.persisted_job.has_value() &&
                finalization.persisted_job->status() == domain::JobStatus::cancelled) {
                result.cancelled_job_ids.push_back(process_exit.job_id);
            }
        } catch (const std::exception& error) {
            result.issues.push_back(issue(
                WorkerRuntimeIssueStage::process_finalization,
                process_exit.job_id,
                error.what()
            ));
        } catch (...) {
            result.issues.push_back(issue(
                WorkerRuntimeIssueStage::process_finalization,
                process_exit.job_id,
                "Unknown worker process finalization failure"
            ));
        }
    }

    result.exited_job_ids.push_back(process_exit.job_id);
    sessions_.erase(iterator);
}

void WorkerRuntime::process_cancellation_requests(WorkerRuntimeCycleResult& result) {
    std::vector<domain::Job> jobs;
    try {
        jobs = job_service_.list();
    } catch (const std::exception& error) {
        result.issues.push_back(issue(
            WorkerRuntimeIssueStage::cancellation, {}, error.what()
        ));
        return;
    } catch (...) {
        result.issues.push_back(issue(
            WorkerRuntimeIssueStage::cancellation,
            {},
            "Unknown cancellation scan failure"
        ));
        return;
    }

    for (const domain::Job& job : jobs) {
        if (job.status() != domain::JobStatus::cancelling) continue;
        const std::string job_id{job.id()};

        bool session_present = false;
        bool termination_already_requested = false;
        {
            std::scoped_lock lock{sessions_mutex_};
            const auto iterator = sessions_.find(job_id);
            if (iterator != sessions_.end()) {
                session_present = true;
                termination_already_requested =
                    iterator->second->cancellation_termination_requested;
            }
        }
        if (termination_already_requested) continue;

        WorkerTerminationResult termination =
            WorkerTerminationResult::not_found;
        try {
            termination = worker_supervisor_.terminate(job_id);
        } catch (const std::exception& error) {
            result.issues.push_back(issue(
                WorkerRuntimeIssueStage::cancellation, job_id, error.what()
            ));
            continue;
        } catch (...) {
            result.issues.push_back(issue(
                WorkerRuntimeIssueStage::cancellation,
                job_id,
                "Unknown cancellation termination failure"
            ));
            continue;
        }

        if (session_present && termination != WorkerTerminationResult::not_found) {
            std::scoped_lock lock{sessions_mutex_};
            const auto iterator = sessions_.find(job_id);
            if (iterator != sessions_.end()) {
                iterator->second->cancellation_termination_requested = true;
            }
            continue;
        }

        if (termination == WorkerTerminationResult::requested) {
            // A process without an ingestion session is an unexpected launch race. Let the
            // supervisor reap it before attempting to finalize state in a later cycle.
            result.issues.push_back(issue(
                WorkerRuntimeIssueStage::cancellation,
                job_id,
                "Cancellation terminated a worker without a registered ingestion session"
            ));
            continue;
        }

        try {
            const auto current = job_service_.find_by_id(job_id);
            if (!current.has_value() || current->status() != domain::JobStatus::cancelling) {
                continue;
            }
            static_cast<void>(job_service_.transition(
                job_id,
                domain::JobStatus::cancelled,
                current->progress(),
                std::nullopt
            ));
            result.cancelled_job_ids.push_back(job_id);
            if (output_artifact_cleanup_service_ != nullptr) {
                try {
                    static_cast<void>(
                        output_artifact_cleanup_service_->quarantine_unregistered_for_job(job_id)
                    );
                } catch (const std::exception& error) {
                    result.issues.push_back(issue(
                        WorkerRuntimeIssueStage::artifact_cleanup, job_id, error.what()
                    ));
                } catch (...) {
                    result.issues.push_back(issue(
                        WorkerRuntimeIssueStage::artifact_cleanup,
                        job_id,
                        "Unknown cancellation partial-output cleanup failure"
                    ));
                }
            }
            if (session_present) {
                std::scoped_lock lock{sessions_mutex_};
                sessions_.erase(job_id);
            }
        } catch (const std::exception& error) {
            result.issues.push_back(issue(
                WorkerRuntimeIssueStage::cancellation, job_id, error.what()
            ));
        } catch (...) {
            result.issues.push_back(issue(
                WorkerRuntimeIssueStage::cancellation,
                job_id,
                "Unknown cancellation persistence failure"
            ));
        }
    }
}

void WorkerRuntime::enforce_heartbeat_timeouts(
    const std::chrono::steady_clock::time_point now,
    WorkerRuntimeCycleResult& result
) {
    std::scoped_lock lock{sessions_mutex_};
    for (auto& [job_id, state_pointer] : sessions_) {
        SessionState& state = *state_pointer;
        if (state.cancellation_termination_requested || state.timeout_termination_requested ||
            state.ingestion.terminal_received() ||
            now - state.last_activity < policy_.heartbeat_timeout) {
            continue;
        }

        try {
            const WorkerTerminationResult termination = worker_supervisor_.terminate(job_id);
            if (termination == WorkerTerminationResult::not_found) {
                result.issues.push_back(issue(
                    WorkerRuntimeIssueStage::termination,
                    job_id,
                    "Heartbeat timeout was detected but the worker process was not found"
                ));
                continue;
            }
            if (termination == WorkerTerminationResult::already_exited) {
                continue;
            }
            state.timeout_termination_requested = true;
            result.timed_out_job_ids.push_back(job_id);
        } catch (const std::exception& error) {
            result.issues.push_back(issue(
                WorkerRuntimeIssueStage::termination,
                job_id,
                error.what()
            ));
            continue;
        } catch (...) {
            result.issues.push_back(issue(
                WorkerRuntimeIssueStage::termination,
                job_id,
                "Unknown worker termination failure"
            ));
            continue;
        }

        try {
            const auto current = job_service_.find_by_id(job_id);
            if (!current.has_value()) {
                result.issues.push_back(issue(
                    WorkerRuntimeIssueStage::persistence,
                    job_id,
                    "Timed-out worker job could not be found for interruption persistence"
                ));
                continue;
            }
            if (domain::occupies_worker_slot(current->status())) {
                static_cast<void>(job_service_.transition(
                    job_id,
                    domain::JobStatus::interrupted,
                    current->progress(),
                    std::nullopt
                ));
                state.timeout_persisted_interrupted = true;
                if (output_artifact_cleanup_service_ != nullptr) {
                    try {
                        static_cast<void>(
                            output_artifact_cleanup_service_->quarantine_unregistered_for_job(
                                job_id
                            )
                        );
                    } catch (const std::exception& error) {
                        result.issues.push_back(issue(
                            WorkerRuntimeIssueStage::artifact_cleanup,
                            job_id,
                            error.what()
                        ));
                    } catch (...) {
                        result.issues.push_back(issue(
                            WorkerRuntimeIssueStage::artifact_cleanup,
                            job_id,
                            "Unknown partial-output cleanup failure"
                        ));
                    }
                }
            }
        } catch (const std::exception& error) {
            result.issues.push_back(issue(
                WorkerRuntimeIssueStage::persistence,
                job_id,
                error.what()
            ));
        } catch (...) {
            result.issues.push_back(issue(
                WorkerRuntimeIssueStage::persistence,
                job_id,
                "Unknown heartbeat timeout persistence failure"
            ));
        }
    }
}

void WorkerRuntime::append_background_issues(std::vector<WorkerRuntimeIssue> issues) {
    if (issues.empty()) return;
    std::scoped_lock lock{background_issue_mutex_};
    append_bounded(
        background_issues_,
        std::move(issues),
        policy_.maximum_background_issues,
        dropped_background_issues_
    );
}

void WorkerRuntime::append_background_observations(
    std::vector<std::string> diagnostics,
    std::vector<WorkerProtocolIssue> protocol_issues
) {
    if (diagnostics.empty() && protocol_issues.empty()) return;
    std::scoped_lock lock{background_issue_mutex_};
    append_bounded(
        background_diagnostics_,
        std::move(diagnostics),
        policy_.maximum_background_diagnostics,
        dropped_background_diagnostics_
    );
    append_bounded(
        background_protocol_issues_,
        std::move(protocol_issues),
        policy_.maximum_background_protocol_issues,
        dropped_background_protocol_issues_
    );
}

void WorkerRuntime::background_main(const std::stop_token stop_token) noexcept {
    while (!stop_token.stop_requested()) {
        try {
            WorkerRuntimeCycleResult cycle = run_cycle();
            append_background_issues(std::move(cycle.issues));
            append_background_observations(
                std::move(cycle.diagnostics),
                std::move(cycle.protocol_issues)
            );
        } catch (const std::exception& error) {
            append_background_issues({issue(
                WorkerRuntimeIssueStage::scheduler, {}, error.what()
            )});
        } catch (...) {
            append_background_issues({issue(
                WorkerRuntimeIssueStage::scheduler,
                {},
                "Unknown autonomous worker runtime failure"
            )});
        }

        std::unique_lock wait_lock{wait_mutex_};
        wait_condition_.wait_for(
            wait_lock,
            policy_.poll_interval,
            [&stop_token] { return stop_token.stop_requested(); }
        );
    }
    running_.store(false, std::memory_order_release);
}

}  // namespace biocore::application
