# Batch Planning v1 — Iteration 082

Baseline: `accepted/iteration-081` / `74f1e2fb374fa326d4595415e15628471a6059a5`.

## Scope

Iteration 082 turns a selected sample set, an exact workflow-template id/version,
parameter overrides and explicit workflow-input assignments into a deterministic
per-sample run-plan preview. It does not submit jobs, create scheduler work, execute
workflow nodes, allocate output paths or implement retry/recovery.

The planner reuses:

- Iteration 080 project/sample registry;
- Iteration 081 sample ↔ managed-input/reference bindings and integrity validation;
- v0.4 exact workflow-template instantiation;
- v0.4 plugin/module binding resolution.

No existing scheduler or DAG behavior is rewritten.

## Deterministic preview

`BatchPlanningService::preview` sorts selected opaque sample ids by binary text order
and sorts input assignments by node/input identity. Semantic request ordering therefore
does not change the resulting preview.

Each selected sample remains visible in the preview. A missing sample, missing binding,
changed input, unavailable role or workflow-binding mismatch is represented as a blocker
on that sample rather than silently removing it.

For a valid sample, the preview contains:

- deterministic planned workflow id;
- exact module id and plugin version for every resolved node;
- canonical resolved parameter name/type/value/source;
- every resolved input, including internal node-output dependencies;
- every external managed-file id, role, type, byte size and SHA-256;
- every expected output port and artifact type.

Warnings from Iteration 081 are preserved. In particular, a FASTQ/reference relation can
remain `not_declared_by_format` without being falsely upgraded to verified reference
compatibility.

## Input assignment

The batch request explicitly maps a workflow node/input port to one of the sample binding
roles: `primary`, `secondary` or `reference`. This avoids guessing R1/R2/reference
semantics from filenames or from two same-type FASTQ inputs.

The same mapping and template are applied to every selected sample. A sample whose binding
cannot satisfy that mapping is invalid and stays visible.

## Approval and explicit exclusion

Approval receives the exact preview that the user saw. Every invalid selected sample must
be explicitly named in the exclusion list. Valid samples may also be explicitly excluded.
An approval with zero included samples is rejected.

Immediately before persistence, every included sample binding is revalidated through the
Iteration 081 service. Every external managed-file snapshot is also compared with the
current binding/file record. A changed, missing or rebound input makes the preview stale
and approval fails.

The workflow template is deliberately **not reloaded during approval**. The approved
snapshot is therefore exactly the previewed plan. If the template file is edited later,
even under the same template id/version, the approved batch plan does not change.

## Immutable persistence

Project schema v13 adds:

- `batch_plans`;
- `batch_plan_samples`;
- `batch_plan_nodes`;
- `batch_plan_parameters`;
- `batch_plan_inputs`;
- `batch_plan_outputs`.

Persistence is transactional. A plan is inserted unsealed, all child snapshots are
written, then the plan is sealed in the same `BEGIN IMMEDIATE` transaction.

Database triggers enforce the sealed state:

- a sealed plan cannot be updated or deleted;
- no child row can be inserted after sealing;
- child rows cannot be updated or deleted;
- sealing requires at least one included sample.

The snapshot intentionally stores content identities rather than foreign-keying managed
files or sample rows. Later registry/file lifecycle changes therefore cannot rewrite
approved history.

## Forward boundary

Iteration 083 may submit the **included** sample snapshots through the existing scheduler.
It must consume the frozen v13 plan rather than re-reading the template or mutable sample
bindings as authoritative planning inputs.

Iteration 082 itself creates no job, workflow execution state, attempt, scheduler queue
entry or generated-output path.
