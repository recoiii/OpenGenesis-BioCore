# Batch Execution v1 — Iteration 083

Baseline: `accepted/iteration-082` / `c1feae1ae874adcf3213f5cbec02050f3fbba28b`.

## Scope

Iteration 083 submits the **included immutable sample snapshots** of an approved v13
batch plan through the existing Job / prepared-execution / JobScheduler / WorkerRuntime
stack. It does not create a second scheduler, rewrite workflow recovery, or implement
retry/resume policy. Recovery and retry remain Iteration 084.

The approved batch plan is authoritative. Submission does not reload the workflow
template and does not re-plan from mutable sample bindings. It materializes each frozen
sample node snapshot into the existing worker `ExecutionPlan` format.

## Submission-time safety

Before a sample Job is persisted, materialization checks:

- the exact module id + plugin version is still available;
- the frozen parameter/input/output contract still matches that exact plugin version;
- each frozen managed-file id/type/size/SHA-256 still matches the current managed-file
  record;
- each managed input passes the existing `IInputFileStorage::verify_managed_file`
  integrity contract.

A changed/missing input or unavailable/incompatible plugin blocks the whole initial
batch submission. No queued sample Jobs are committed in that case.

Execution-plan JSON files are published before the SQLite transaction, consistent with
the existing prepared-Job design. If the database transaction fails normally, all
newly published snapshots are discarded. A process crash between file publication and
database commit can leave a non-runnable orphan snapshot; durable restart reconciliation
is deliberately owned by Iteration 084 recovery/hardening and is not represented as a
successful submission.

## Exactly-once batch submission boundary

Schema v14 adds a durable batch execution ledger keyed by approved `plan_id`.
`SqliteBatchExecutionStore::add` uses one `BEGIN IMMEDIATE` transaction to:

1. claim the approved plan id;
2. insert every queued sample Job;
3. insert each immutable prepared execution-plan association;
4. link sample → Job in ordinal order;
5. seal the batch execution record.

Therefore a committed approved plan has one batch submission ledger. Repeating submit
with the same concurrency returns the existing Job set instead of creating a second run.
A concurrent loser cannot leave partial Jobs because the whole transaction rolls back.
Re-submission with a different concurrency limit is rejected rather than silently
changing the already-submitted execution policy.

## Existing scheduler and concurrency

Batch concurrency is implemented as an optional policy on the existing
`JobScheduler`. Each batch Job resolves to a scheduling group equal to its plan id and
a stored maximum concurrency.

The scheduler first applies its existing global worker-slot accounting. It then defers
queued batch Jobs whose group already occupies its batch quota. Thus:

- there is still one scheduler and one worker-runtime path;
- the configured global scheduler maximum remains the absolute upper bound;
- a batch limit can only reduce concurrency within that global capacity;
- non-batch Jobs retain their previous scheduling behavior.

Quota-deferred Job ids are exposed in `JobSchedulerTickResult` for observability.

## Output namespace

Materialized output paths use the existing reserved pattern:

`outputs/<job-id>--<node-id>--<port>.out`

Every sample receives a unique Job id inside the atomic submission, so sample output
paths cannot collide even when node ids and port names are identical across samples.

## Partial failure

Every sample is an independent existing Job. A Job failure affects only that Job.
Batch status is derived from the linked Job states:

- work remaining → `active`;
- all completed → `completed`;
- all failed → `failed`;
- mixed completed/failed terminal results → `partial_failure`;
- interrupted work requiring a later recovery decision → `attention`.

Iteration 083 never automatically retries a failed/interrupted sample and never cancels
siblings because one sample failed.

## Cancellation

A batch cancellation request is durable and monotonic.

- queued/draft/interrupted Jobs transition directly to `cancelled`;
- preparing/running/paused Jobs transition to `cancelling`;
- already terminal/cancelling Jobs are left unchanged.

The existing WorkerRuntime already observes `cancelling`, calls the existing worker
supervisor termination path, and finalizes the process as `cancelled`. Iteration 083
reuses that behavior rather than introducing a batch-specific process terminator.

Repeated batch cancellation is idempotent.

## Persistence

Schema v14 adds:

- `batch_executions`;
- `batch_execution_jobs`;
- scheduling and immutability constraints/triggers.

A batch execution must reference a sealed approved plan. Only samples marked
`included` in that plan may receive Jobs. Once sealed, execution identity, concurrency,
sample-to-Job links and submission timestamps are immutable. The only permitted parent
mutation is the monotonic cancellation-request transition.

## Forward boundary

Iteration 084 owns restart reconciliation, resume versus retry semantics, new attempt
lineage and checkpoint/input-identity safety. Iteration 083 intentionally does not
auto-resume or create retry attempts.
