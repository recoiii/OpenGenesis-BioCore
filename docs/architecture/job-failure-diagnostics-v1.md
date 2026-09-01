# Job Failure Diagnostics v1

Iteration 047 makes the current reason for a failed or interrupted Job durable. Live worker logs remain
bounded, browser-memory telemetry; they are not used as the authoritative failure record.

## Domain contract

`JobFailure` contains a validated kind, message, optional native exit code, optional worker timestamp,
and the Core timestamp at which the evidence was recorded. The supported kinds are:

- `worker_reported_failure` for a worker `failed` lifecycle event;
- `process_exit_without_terminal` for a native exit without `completed` or `failed`;
- `heartbeat_timeout` for Core-enforced liveness termination;
- `startup_recovery` for a stale worker-owned Job found during project startup;
- `unspecified_terminal_failure` for older Application callers that lack a more specific diagnostic;
- `legacy_terminal_state` for schema-v6 failed/interrupted rows migrated to schema v7.

Worker-reported failures require a non-zero exit code and worker timestamp. Process-exit evidence
requires the observed native exit code and cannot claim a worker timestamp. Timeout, recovery,
unspecified, and legacy evidence cannot carry an exit code or worker timestamp.

## Atomic persistence

Schema v7 stores the current evidence directly on the `jobs` row. `SqliteJobRepository` updates status,
progress, timestamps, revision, and all failure columns in one optimistic `UPDATE`. The insert and update
triggers reject:

- failed/interrupted rows without complete evidence;
- other statuses with any failure column populated;
- worker failure evidence on a status other than `failed`;
- process-exit, timeout, or startup-recovery evidence on a status other than `interrupted`;
- exit-code or worker-timestamp fields that do not match their evidence kind.

Re-queuing an interrupted Job clears all failure columns in the same runtime-state update. The model
retains only the current failure; unbounded failure history is outside this contract.

## Migration and recovery

The v6-to-v7 migration runs inside the existing immediate transaction. Existing failed and interrupted
rows receive `legacy_terminal_state`, a fixed explanatory message, and a recorded time selected from
`finished_at_utc` then `updated_at_utc`. Other rows are unchanged.

At runtime, worker `failed` messages preserve their message, exit code, and worker timestamp. A process
exit without a terminal lifecycle event records the observed exit code. Heartbeat enforcement and
startup recovery use distinct evidence kinds so the two interruption mechanisms remain distinguishable
after restart.

## Presentation

Job list/detail JSON and WebSocket snapshots expose a nullable `failure` object. JSON and HTML execution
reports use the same durable Job value. The browser detail view labels the diagnostic as durable and
keeps it visually separate from ephemeral live logs.

## Preserved boundaries

Worker Protocol remains v2. Plugin and pipeline identities, biological algorithms and outputs,
loopback-only networking, browser-session isolation, token separation, process-tree cancellation, and
artifact integrity behavior are unchanged.
