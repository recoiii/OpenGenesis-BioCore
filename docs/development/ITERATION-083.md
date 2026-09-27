# Iteration 083 — Batch Execution

Status: CANDIDATE, awaiting independent Gemini review. Not accepted or frozen.
Baseline: `accepted/iteration-082` / `c1feae1ae874adcf3213f5cbec02050f3fbba28b`.
Development branch: `v0.5.0-dev`.
Development identity: `0.5.0-dev`.

## Objective

Submit every included sample from an immutable approved batch plan through the existing
scheduler/runtime path with durable submit idempotency, per-batch concurrency limits,
cancellation and partial-failure isolation.

## Deliverables

- approved-plan → existing worker `ExecutionPlan` materialization;
- submission-time frozen input/plugin contract verification;
- atomic plan-idempotent v14 batch execution ledger;
- queued Job + prepared execution creation in the same SQLite transaction;
- optional per-batch quota provider integrated into the existing `JobScheduler`;
- unique Job-namespaced output paths;
- batch status aggregation without sibling failure propagation;
- durable idempotent batch cancellation reusing existing Job/WorkerRuntime semantics;
- frozen v13 migration fixture;
- dedicated submit, quota, outputs, partial-failure, cancel, persistence, migration
  and rollback tests;
- retained Iterations 079–082 and v0.4 regression coverage.

## Acceptance gates

1. Repeating submission of the same approved plan cannot create a second Job set.
2. Initial submission is all-or-none; a Job-id conflict or validation failure leaves no
   partial batch ledger/queued Job set.
3. Batch concurrency never exceeds the submitted batch limit or the existing global
   scheduler capacity.
4. Output paths are disjoint across samples even when node/port names are identical.
5. One sample failure does not cancel or mutate sibling Jobs; mixed terminal outcomes
   remain visible as partial failure.
6. Batch cancellation cancels queued work and routes active work through existing
   `cancelling` / WorkerRuntime termination semantics; repeated cancel is idempotent.
7. Submission consumes the frozen v13 plan rather than reloading the template or
   re-planning from mutable bindings.
8. v13→v14 migration preserves approved plans and prior project/workflow state.
9. Injected late v14 migration failure rolls back all new schema objects.
10. Iteration 084 recovery/retry behavior remains out of scope.
11. Independent Gemini ACCEPT on the exact candidate/source SHA is mandatory before freeze.

Project owner/developer: Recep Çelik. AI-assisted architecture, implementation,
validation and review-package preparation: ChatGPT. Independent Gemini review is pending.
