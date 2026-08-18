# Autonomous Worker Runtime v1

## Status

Iteration 011 candidate architecture.

## Purpose

`WorkerRuntime` is an Application-owned orchestration service that repeatedly joins the already
accepted scheduling, process-supervision, wire-protocol, event-ingestion, and job-persistence
boundaries. It contains no native process API, SQLite statement, JSON parser, or filesystem code.

## Dependency direction

```text
WorkerRuntime (Application)
  -> JobScheduler
  -> JobService
  -> IWorkerSupervisor
  -> IMonotonicClock
  -> WorkerEventIngestionSession

PlatformWorkerSupervisor (Infrastructure)
  -> implements IWorkerSupervisor
  -> posix_spawn / waitpid / kill on POSIX
  -> CreateProcessW / WaitForSingleObject / TerminateProcess on Windows
```

## Deterministic cycle order

One `run_cycle()` performs:

1. non-blocking stdout/stderr polling;
2. non-blocking native process reap;
3. heartbeat timeout enforcement;
4. scheduler tick with externally reserved process slots;
5. ingestion-session registration for successful launches.

Output and exit processing happen before scheduling so that newly available capacity is observed
without delaying a full cycle. A process that has produced a terminal event or has been forcefully
terminated but has not yet been reaped remains a reserved worker slot. This prevents the database
terminal state from opening capacity before the native process is gone.

## Session ownership

Each successful launch creates exactly one `WorkerEventIngestionSession`, keyed by Job ID and bound
to the persisted launch revision. The runtime owns the session until the matching native process is
reaped. Output for an unknown Job ID is rejected as a runtime issue.

## Liveness policy

Liveness is measured with `IMonotonicClock`, never with worker-supplied UTC timestamps or the system
wall clock. The receipt of a valid `ready`, `heartbeat`, `progress`, or `log` event refreshes the
last-activity point. `completed` and `failed` remain subject to native-exit cross-validation.

Policy constraints:

- poll interval must be positive;
- heartbeat timeout must be positive;
- heartbeat timeout must not be shorter than the poll interval.

At timeout, the runtime requests forced termination through `IWorkerSupervisor`. If termination is
accepted, an active Job is persisted as `interrupted`. The process remains tracked and reserves a
slot until reap. If the process had already exited, the supervisor returns `already_exited`; the
runtime does not overwrite the eventual terminal result and waits for the cached native exit.

## Native termination semantics

Iteration 011 uses the smallest termination boundary required for heartbeat recovery:

- POSIX: `kill(pid, SIGKILL)`;
- Windows design: `TerminateProcess(handle, 137)`.

Before forced termination, the concrete supervisor performs a non-blocking native exit query. An
already-finished child has its exit code cached and is returned by the next `reap_exited()` call.
This closes the race in which a successfully completed process exits immediately before timeout
handling.

Process groups, Windows Job Objects, child-tree termination, graceful control messages, and user
cancellation are deferred.

## Background service

`start()` owns a `std::jthread` that calls `run_cycle()` at the configured poll interval. `stop()`
requests stop, wakes the condition variable, and joins the thread. Start is single-owner and a second
start is rejected. An atomic cycle guard rejects overlapping manual/background cycles.

Cycle exceptions are isolated and retained as typed `WorkerRuntimeIssue` values. Diagnostics and
wire-protocol issues are retained in separate bounded-by-producer in-memory queues and exposed via
drain APIs. Durable logging is not part of this iteration.

## Capacity accounting

`JobScheduler::tick(externally_reserved_slots)` combines:

- database Job states that occupy a worker slot; and
- native processes whose Job state no longer occupies a slot but whose process is not yet reaped.

The sum is saturated at the configured maximum, preventing arithmetic overflow and transient native
process overcommit.

## Explicit boundaries

Iteration 011 does not provide:

- graceful stdin cancellation/control protocol;
- heartbeat persistence or restart recovery;
- kill escalation or process-tree ownership;
- cross-process runtime leadership/lease;
- durable diagnostics/log storage;
- CPU, memory, or disk limits;
- native Windows/MSVC execution validation;
- autonomous reconstruction of sessions after Core crash;
- terminal-event-to-native-exit grace timeout distinct from heartbeat policy.
