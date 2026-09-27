# Iteration 084 — Recovery & Retry

Status: CANDIDATE, awaiting independent Gemini review. Not accepted or frozen.
Baseline: `accepted/iteration-083` / `09ae81ef024f06cc5e6a7c60a4298220452aa3c6`.
Development branch: `v0.5.0-dev`.
Development identity: `0.5.0-dev`.

## Objective

Reconcile persisted batch execution state after restart, distinguish safe checkpoint
resume from explicit full retry, and record every recovery action as a new immutable
attempt linked to its predecessor without weakening the Iteration 083 scheduler,
execution-plan or frozen-input contracts.

## Deliverables

- SQLite schema v15 batch-attempt lineage and per-attempt execution-node snapshots;
- v14 -> v15 backfill of every historical root batch Job as `attempt_number=1`, `initial`;
- immutable sequential parent/child attempt constraints;
- latest-attempt-aware batch snapshot, cancellation and scheduler quota lookup;
- restart inspection that creates no Jobs and performs no automatic retry/resume;
- durable checkpoint reconstruction from Job state plus registered generated outputs;
- reuse of the existing `WorkflowResumePlanner` checkpoint contract;
- partial resume plans that omit verified completed nodes and bind their historical outputs;
- checkpoint-complete recovery that seals a completed child attempt without launching a worker;
- explicit retry as a new Job containing the complete frozen sample plan;
- frozen input/plugin validation before either resume or retry;
- startup wiring that reports samples requiring explicit recovery action;
- retained Iteration 083 and v0.4 regression coverage.

## Acceptance gates

1. Restart inspection restores persisted batch/sample state without automatically rerunning work.
2. A successfully completed sample is never auto-rerun; an interrupted sample with fully verified
   durable outputs can be sealed complete without a worker launch.
3. Resume uses the existing workflow checkpoint planner and only executes nodes that are not safely
   reusable under that contract.
4. Every resume/retry creates a new immutable batch attempt with a new Job id and a parent link to
   the immediately preceding attempt.
5. Explicit retry rebuilds the complete execution plan from the immutable approved sample snapshot
   rather than mutating or reusing the failed Job identity.
6. Changed/missing managed input identity or failed input integrity verification blocks recovery;
   an old checkpoint is never silently reused against changed inputs.
7. A checkpoint artifact that fails the existing verifier is not reused; affected nodes are
   recomputed according to `WorkflowResumePlanner` dependency propagation.
8. Recovery child Jobs remain subject to the original batch scheduling group and concurrency quota.
9. Batch snapshots and cancellation target the latest attempt for each sample while preserving all
   historical attempts unchanged.
10. v14 -> v15 migration preserves existing execution state, backfills initial lineage, and an
    injected late v15 failure rolls back all v15 objects atomically.
11. Independent Gemini ACCEPT on the exact candidate/source SHA is mandatory before freeze.

Project owner/developer: Recep Çelik. AI-assisted architecture, implementation,
validation and review-package preparation: ChatGPT. Independent Gemini review is pending.
