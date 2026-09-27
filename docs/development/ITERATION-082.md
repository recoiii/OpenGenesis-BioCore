# Iteration 082 — Batch Planning

Status: CANDIDATE, awaiting independent Gemini review. Not accepted or frozen.
Baseline: `accepted/iteration-081` / `74f1e2fb374fa326d4595415e15628471a6059a5`.
Development branch: `v0.5.0-dev`.
Development identity: `0.5.0-dev`.

## Objective

Produce a deterministic, inspectable and immutable per-sample batch plan from an exact
workflow template and the Iteration 081 sample bindings, without beginning execution.

## Deliverables

- deterministic selected-sample ordering and workflow identities;
- explicit primary/secondary/reference → workflow-input assignments;
- exact per-sample input SHA-256/size/type snapshots;
- canonical resolved parameters and expected outputs;
- visible sample-level blocker/warning diagnostics;
- explicit exclusion requirement for invalid samples;
- approval-time stale binding/input revalidation;
- approval of the exact preview without template reload;
- immutable transactional schema v13 batch-plan persistence;
- frozen v12 migration fixture;
- dedicated deterministic, preview, exclusion, stale, template-freeze,
  persistence, migration and rollback tests;
- retained Iterations 079–081 and v0.4 regression coverage.

## Acceptance gates

1. Reordering selected samples or equivalent input assignments does not change the preview.
2. Every selected sample appears in preview; invalid samples are never silently dropped.
3. Valid samples expose exact inputs, canonical parameters and expected outputs.
4. Every invalid sample must be explicitly excluded before approval.
5. Included samples are revalidated at approval; changed inputs reject the stale preview.
6. Editing the template after preview does not mutate the approved plan.
7. Sealed plans cannot be updated, extended or deleted and round-trip without loss.
8. v12→v13 migration preserves existing project/sample/binding/workflow data.
9. Injected v13 migration failure rolls back all new schema objects.
10. No scheduler submission, execution, retry or Iteration 083+ behavior is introduced.
11. Independent Gemini ACCEPT on the exact candidate/source SHA is mandatory before freeze.

Project owner/developer: Recep Çelik. AI-assisted architecture, implementation,
validation and review-package preparation: ChatGPT. Independent Gemini review is pending.
