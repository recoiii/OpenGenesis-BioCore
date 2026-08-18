# Job Scheduler v1

Iteration 008 introduces Application-owned job scheduling without launching an operating-system
process. The scheduler depends on `JobService` and the abstract `IWorkerSupervisor` port.

## Boundary

- Domain owns job states, priorities, transitions, and worker-slot classification.
- Application owns `JobScheduler`, `IWorkerSupervisor`, `WorkerLaunchRequest`, and scheduler errors.
- Infrastructure does not yet provide a Windows or Linux process supervisor.
- Executables do not yet compose or run the scheduler.

No worker-protocol transport, process identifier, heartbeat, cancellation signal, or recovery
record is introduced in this iteration.

## Tick model

A scheduler tick performs the following steps:

1. Read the current project-local jobs through `JobService`.
2. Count `preparing`, `running`, `paused`, and `cancelling` jobs as occupied worker slots.
3. Calculate available capacity from the configured positive maximum.
4. Select queued jobs in deterministic order:
   - high priority before normal priority before low priority;
   - earlier `created_at_utc` before later values;
   - lexicographically smaller job ID as the final tie-breaker.
5. Transition one selected job from `queued` to `preparing` using the existing optimistic-revision path.
6. Submit a `WorkerLaunchRequest` containing job metadata and the newly persisted job revision.
7. Count a slot only after the supervisor accepts the launch request.

A stale queued snapshot that loses its optimistic update is skipped. The scheduler continues to
later queued jobs so an available slot is not unnecessarily left empty.

## Launch failure

If `IWorkerSupervisor::launch` throws, the scheduler transitions the prepared job to `failed` and
continues searching for another queued job. If the failure state cannot be persisted, the scheduler
throws `JobSchedulerError::launch_failure_recovery_failed`; the job remains `preparing` and must be
handled by a later recovery mechanism.

The current job schema does not yet persist a launch-error message or worker identity.

## Tick exclusion

A scheduler instance uses an `std::atomic_flag` guard. A concurrent or reentrant call to `tick()` is
rejected with `tick_already_in_progress`, and RAII clears the guard on every exit path.

This guard protects one scheduler object. Iteration 008 still requires exactly one scheduler owner
per project. Multiple processes or multiple independently constructed scheduler instances are not
coordinated by a database lease and could exceed the configured capacity.

## Explicit exclusions

- Windows or Linux process launch
- Worker PID or process-handle persistence
- Multi-process scheduler leadership or database leasing
- Worker ready/heartbeat/progress/completion event handling
- Cancellation and shutdown signalling
- Interrupted-job recovery
- Resource-aware CPU, memory, or disk allocation
- Queue pagination, aging, or user-adjustable ordering policies
