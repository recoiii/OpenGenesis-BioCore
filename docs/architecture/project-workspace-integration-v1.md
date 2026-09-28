# Project Workspace Integration v1 — Iteration 087

Baseline: `accepted/iteration-086` / `052304fffc5d14ad6fb67858cad48b15cd4ca9ce`.

## Purpose

Iteration 087 is the user-flow integration layer for v0.5. It does not replace the project,
sample, batch, workflow, recovery, results or report engines introduced in Iterations 079–086.
Instead it composes those accepted services behind one authenticated current-project workspace and
one browser surface.

The supported end-to-end path is:

`project init/open -> sample import -> input binding -> batch preview -> explicit approval -> batch submit -> recovery/retry -> results -> verified JSON/HTML report`.

Project creation remains owned by the existing `biocore init` bootstrap because the running local
server is deliberately bound to one validated project root. Iteration 087 does not introduce unsafe
runtime project-root switching or cross-project database ownership.

## Current-project workspace facade

`ProjectWorkspaceIntegrationService` composes existing services:

- `SampleRegistryImportService`;
- `SampleBindingService`;
- `BatchPlanningService`;
- `BatchExecutionService`;
- `BatchRecoveryService`;
- `BatchResultsService`.

The facade adds no second scheduler, DAG implementation, result parser or retry mechanism.

`SqliteCurrentProjectStore` reads the immutable singleton `project_metadata` row already owned by
the project database. Project schema remains v15.

## Workspace snapshot

The read model contains:

- current project identity and optional research metadata;
- samples and current bindings;
- explicit broken-link issues when a binding references a missing managed file;
- managed input files and an `orphaned` flag when no sample binding references the file;
- approved batch plans, current execution snapshots and recovery-attention counts.

An orphan flag is diagnostic only. Iteration 087 does not silently delete, reassign or mutate the
managed file.

## Browser workflow

The existing frontend gains a Project Workspace surface. All persisted or user-provided values are
rendered through DOM `textContent`, not injected HTML.

The surface supports:

1. current-project context and a guide explaining that project creation/opening happens before
   `serve` through the existing validated project bootstrap;
2. CSV/TSV sample import preview and commit;
3. sample binding preview and commit against existing managed inputs;
4. exact workflow-template selection and per-sample batch preview;
5. explicit exclusion of invalid samples before approval;
6. immutable approval and submission through the existing scheduler;
7. current batch execution/recovery state;
8. explicit resume/retry actions when the recovery service permits them;
9. lineage-aware results and links to Iteration 086 verified JSON/HTML report exports;
10. direct navigation to the already accepted Workflow Builder and Execution Workspace surfaces.

The workspace does not create a parallel WebSocket/runtime channel. Existing lifecycle telemetry
remains owned by the accepted job/execution UI.

## Local API

Authenticated routes are namespaced under `/api/v1/project-workspace`:

- `GET /api/v1/project-workspace`
- `POST /api/v1/project-workspace/samples/import/{csv|tsv}/preview`
- `POST /api/v1/project-workspace/samples/import/{csv|tsv}/commit`
- `POST /api/v1/project-workspace/samples/{sample-id}/binding/preview`
- `POST /api/v1/project-workspace/samples/{sample-id}/binding/commit`
- `POST /api/v1/project-workspace/batches/preview`
- `POST /api/v1/project-workspace/batches/{plan-id}/approve`
- `POST /api/v1/project-workspace/batches/{plan-id}/submit`
- `POST /api/v1/project-workspace/batches/{plan-id}/cancel`
- `GET /api/v1/project-workspace/batches/{plan-id}`
- `GET /api/v1/project-workspace/batches/{plan-id}/recovery`
- `GET /api/v1/project-workspace/batches/{plan-id}/results`
- `POST /api/v1/project-workspace/batches/{plan-id}/samples/{sample-id}/{resume|retry}`

Iteration 086 report routes remain authoritative for package export:

- `/api/v1/batches/{plan-id}/export-manifest.json`
- `/api/v1/batches/{plan-id}/report.html`

## Approval and preview safety

Batch approval requires the same process-local preview produced by the current workspace service.
A client cannot manufacture an approved plan by posting an arbitrary approval payload without a
server-side preview. The cache is bounded and consumed on approval.

The accepted Iteration 082 service still revalidates current included bindings/files before
approval and freezes the exact previewed template/parameter plan.

## User-visible report provenance

Iteration 087 strengthens the human-readable batch report sample table to display latest Job ID,
latest Job status, attempt number and attempt mode. The JSON package already carried these fields;
this change aligns the HTML E2E surface with the same package provenance without changing package
schema or scientific content.

## End-to-end evidence

`integration.project_workspace_v05_e2e` uses a real in-memory SQLite v15 project and accepted v0.5
services to prove:

- an unbound managed input is visible as orphaned;
- sample import persists through the existing registry;
- missing binding is explicitly explained;
- binding validation removes the orphan condition without changing file ownership;
- exact-template batch preview/approval/submission produces an initial Job;
- a failed sample is classified as explicit retry;
- retry creates a new immutable child attempt and leaves the failed parent unchanged;
- latest results resolve the retry attempt;
- the Iteration 086 package/report layer consumes that completed result without leaking the absolute
  project root.

## Explicit boundaries

Iteration 087 does not add:

- database migration beyond v15;
- runtime project-root switching;
- new biological/statistical methods;
- a new scheduler, DAG engine, recovery planner or result parser;
- dynamic cross-sample fan-in/fan-out;
- cloud/multi-user access;
- ZIP/TAR scientific-data archives;
- release tagging or native Windows release closure.

Those release-hardening concerns belong to Iteration 088.
