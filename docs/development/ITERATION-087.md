# Iteration 087 — Project Workspace & E2E Integration

Baseline: `accepted/iteration-086` / `052304fffc5d14ad6fb67858cad48b15cd4ca9ce`.

Status: candidate only until independent Gemini ACCEPT.

## Goal

Expose the accepted v0.5 Project & Sample Workspace workflow as one coherent browser/API flow while
preserving the ownership and invariants of Iterations 079–086.

## Scope

- add a current-project workspace integration facade over existing sample/binding/batch/recovery/results services;
- expose authenticated current-project workspace API routes;
- add a Project Workspace browser surface with explicit empty/error states and user guidance;
- surface orphan managed inputs and broken sample-file bindings without mutating ownership;
- bridge the workspace to the existing Workflow Builder and Execution Workspace;
- expose explicit batch retry/resume and Iteration 086 report links;
- add one real SQLite/service-chain v0.5 E2E test;
- keep project schema at v15 and binary identity at `0.5.0-dev`.

## Acceptance gates

1. The running workspace resolves exactly the project already validated/opened by the local server; no hidden cross-project switch is introduced.
2. Sample import preview/commit reuses `SampleRegistryImportService` and malformed/duplicate input remains fail-closed.
3. Sample binding preview/commit reuses `SampleBindingService`, including current integrity/reference checks.
4. Workspace snapshot explicitly identifies missing/broken binding targets and orphan managed inputs; it never silently deletes or reassigns files.
5. Batch preview/approval reuses Iteration 082 and approval requires a current server-side preview; invalid samples require explicit exclusion.
6. Batch submission/cancellation reuses Iteration 083 scheduler/execution semantics and does not introduce another runtime.
7. Recovery UI/API exposes only the actions classified by Iteration 084 and resume/retry continue to create the accepted attempt lineage.
8. Results and report links consume Iterations 085–086; latest retry attempt provenance is visible in the human-readable HTML report as well as JSON.
9. Browser rendering of persisted/user content uses safe DOM text insertion; the 087 workspace adds no independent WebSocket/scheduler ownership.
10. A dedicated E2E test proves import -> binding -> preview -> approval -> submit -> failed attempt -> retry -> completed results -> report on one SQLite v15 project.
11. Full four-lane candidate CI must pass before Gemini review; no freeze occurs without exact `VERDICT: ACCEPT` for the exact candidate/source SHA.

## Project creation boundary

The roadmap begins with project creation. The existing `biocore init` bootstrap remains the project
creation owner. `biocore serve` is intentionally rooted in one validated project, so Iteration 087
does not add runtime root creation/switching to a process already serving another project. The UI
explains this boundary and then completes the remaining current-project flow.

## Explicit non-goals

- no schema migration;
- no scientific algorithm/metric changes;
- no new workflow engine/scheduler;
- no cross-sample dynamic DAG fan-in;
- no multi-user/cloud project switcher;
- no release packaging/tagging/native-Windows closure (Iteration 088).

## Review rule

The candidate remains unaccepted and unfrozen until Gemini independently returns exact
`VERDICT: ACCEPT`. Any blocking finding is fixed inside Iteration 087 and the entire candidate CI is
rerun before resubmission.
