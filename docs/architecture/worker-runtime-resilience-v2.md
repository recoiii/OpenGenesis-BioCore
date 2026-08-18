# Worker Runtime Resilience v2 — Long Jobs, Cancellation and Bounded Control Plane

## Scope

Iteration 041 hardens the existing local worker/runtime architecture for long-running and noisy native
analysis processes. It does not change Worker Protocol v2, the execution-plan schema, the database
schema, plugin trust boundaries, or the localhost-only security model.

## Long-running plugin liveness

`biocore-worker` emits lifecycle events through one sequence-serialized `LifecycleEmitter`. While a
native plugin child is synchronously executing, a `PluginHeartbeatGuard` emits an explicit heartbeat
approximately once per second. Stopping the guard is condition-variable driven so a fast plugin does
not incur an artificial one-second completion delay. An asynchronous heartbeat-emission failure is
captured and rethrown on the worker thread instead of terminating the process from the heartbeat
thread.

Core liveness is intentionally refreshed only by `ready`, `heartbeat`, and `progress`. `log` and
`artifact` events are not liveness signals, so a worker that only spams output cannot indefinitely mask
a stalled control plane.

## Cancellation contract

`POST /api/v1/jobs/<jobId>/cancel` is the local authenticated cancellation command.

- `draft`, `queued`, and `interrupted` jobs transition directly to `cancelled` without launching a worker.
- `preparing`, `running`, and `paused` jobs transition to `cancelling`.
- the runtime requests native-process termination for `cancelling` sessions;
- process exit after a requested cancellation finalizes the job as `cancelled`, not `interrupted`;
- completed/failed jobs are not cancellable;
- repeated cancellation of `cancelling`/`cancelled` jobs is idempotent.

Cancellation is currently **hard process termination**, not cooperative per-plugin cancellation. A
plugin does not receive a cleanup callback. Existing partial/unregistered output cleanup and quarantine
rules therefore remain authoritative for cancelled work.

## Bounded worker output draining

One supervisor poll cannot drain an arbitrarily large stream from a noisy worker. Current per-drain
budgets are:

- stdout lifecycle stream: 1 MiB;
- stderr diagnostics stream: 256 KiB.

The supervisor returns to the runtime when the budget is exhausted, preserving scheduling fairness and
allowing OS pipe backpressure to bound producer progress. If a child exits while pipe data remains, the
process exit is not published until both streams have been drained, preventing tail diagnostics or
lifecycle events from being silently discarded.

Existing per-line protocol/diagnostic limits remain in force.

## Bounded background observation retention

When the runtime is used without an immediate consumer, background observations retain the newest N
records rather than growing without bound. Defaults are:

- issues: 1024;
- diagnostics: 1024;
- protocol issues: 1024.

Draining after eviction returns the retained newest records plus one synthetic notice reporting how many
older records were dropped. These caps apply to the runtime's observation buffers; they are not OS-level
CPU/RAM/disk quotas for plugin children.

## Explicit non-goals

Iteration 041 does not claim:

- CPU-time, address-space/RAM, filesystem quota, or cgroup/job-object resource enforcement;
- free-disk preflight guarantees for every output size;
- resumable/cooperative plugin cancellation;
- automatic checkpoint/resume of an interrupted or cancelled plugin;
- distributed/remote worker supervision.

Those require separate platform/resource-policy contracts and are not inferred from the bounded local
control-plane work in this iteration.
