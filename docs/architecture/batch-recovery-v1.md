# Batch Recovery v1 — Iteration 084

Baseline: `accepted/iteration-083` / `09ae81ef024f06cc5e6a7c60a4298220452aa3c6`.

## Recovery principle

Iteration 084 does not add a second runtime or scheduler. The existing Job,
`JobScheduler`, WorkerRuntime, immutable approved batch plan and workflow checkpoint
planner remain authoritative. Recovery adds a durable **batch attempt lineage** around
those existing contracts.

A restart first uses the existing project recovery path to mark stale active Jobs as
`interrupted`. Batch recovery then inspects persisted executions and computes an action
for each latest sample attempt. Inspection is read-only: no Job is automatically resumed
or retried.

## Attempt lineage

Schema v15 adds:

- `batch_execution_attempts` — `(plan_id, sample_id, attempt_number)` -> new Job identity,
  parent Job identity, mode (`initial`, `resume`, `retry`) and creation timestamp;
- `batch_execution_attempt_nodes` — immutable ordered frozen-plan node identities executed
  by that attempt.

The original Iteration 083 `batch_execution_jobs` row remains the immutable root link.
Migration backfills each root Job as attempt 1 / `initial`, including its frozen plan
nodes. New attempts are sequential and must point at the immediately preceding Job.
Lineage rows and their node lists cannot be updated or deleted.

## Resume versus retry

An interrupted Job is checkpoint-reconstructed from its immutable attempt-node scope,
Job progress/failure evidence and registered generated-output provenance. The resulting
`WorkflowCheckpointManifest` is passed to the existing `plan_workflow_resume` function.

- verified completed node -> `reuse_completed`;
- pending/interrupted/invalid-output node -> execute/retry according to the existing planner;
- recomputed upstream node invalidates downstream reuse through the existing DAG rule;
- exhausted or unavailable required dependency -> recovery is blocked.

A **resume** creates a new Job containing only the nodes the planner says must execute.
Inputs whose producer is reused are rebound to the verified historical generated-output
path, and the removed producer is no longer a runtime dependency.

A **retry** creates a new Job and rematerializes the complete frozen sample plan. The
failed/interrupted historical Job remains unchanged.

If every node is already durably complete and verified after a crash window, resume
creates a lineage child already marked `completed` and no execution-plan snapshot is
published. Successful work is therefore not launched a second time merely to repair the
parent Job's interrupted terminal state.

## Input identity boundary

Before resume or retry, the same Iteration 083 materializer validation is executed
against the frozen approved sample snapshot:

- exact module id and plugin version must remain available;
- frozen parameter/input/output contracts must still match;
- managed-file id/type/size/SHA-256 must match the frozen snapshot;
- `IInputFileStorage::verify_managed_file` must return `verified`.

If this boundary fails, recovery is blocked. The caller must produce a new approved plan
rather than silently applying an old checkpoint to changed scientific inputs.

## Scheduler and cancellation continuity

`SqliteBatchExecutionStore::quota_for_job` resolves every lineage Job through
`batch_execution_attempts`, so resume and retry children retain the original batch
scheduling group and maximum concurrency.

Batch snapshots select the highest attempt number per sample. Cancellation also targets
only that latest Job, while historical attempts remain immutable audit evidence.

## Migration safety

The v15 migration is additive. Existing v14 batch executions and root Jobs are preserved
and backfilled. New schema objects and the version marker are created inside the existing
migration transaction, so injected failure rolls the database back to an intact v14
state.

## Forward boundary

Iteration 084 provides backend recovery/retry semantics and startup reconciliation.
Results/QC aggregation remains Iteration 085; report-bundle provenance remains Iteration
086; integrated project workspace UX remains Iteration 087; Windows/release closure
remains Iteration 088.
