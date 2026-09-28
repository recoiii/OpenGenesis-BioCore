# OpenGenesis-BioCore v0.5.0 Release Notes

OpenGenesis-BioCore v0.5.0 is the **Project & Sample Workspace** release. It builds on the v0.3 canonical
small-variant layer and v0.4 Workflow Engine 2.0 without replacing either. The release adds durable project/sample
context, deterministic multi-sample batch orchestration, explicit recovery lineage, lineage-aware QC/results and an
integrated local browser workspace.

## Major changes since v0.4.0

- Revision-controlled project research metadata layered onto the existing project/catalog/workspace ownership model.
- Durable sample registry with bounded CSV/TSV import preview/commit behavior.
- Explicit sample primary/secondary/reference binding with managed-file integrity and reference compatibility checks.
- Deterministic immutable batch plans from exact-version workflow templates, including explicit invalid-sample
  exclusion before approval.
- Batch execution through the existing scheduler/runtime with per-batch concurrency limits, idempotent submission,
  collision-safe output naming, cancellation and partial-failure aggregation.
- Crash/restart reconciliation plus explicit resume/retry semantics. Every recovery action creates a new Job identity
  in immutable immediate-parent attempt lineage; completed scientific work is not silently rerun.
- Lineage-aware current result selection and existing-plugin QC aggregation. Missing metrics are not imputed as zero;
  incompatible plugin/schema/parameter/reference contracts are not silently compared.
- Guarded bridge from compatible terminal single-sample VCF outputs into the existing v0.3 VCF ingestion and
  `MultiSampleMatrix` implementation.
- Integrity-verified batch result package snapshots with deterministic JSON manifests and self-contained HTML reports.
- Integrated Project Workspace UI joining sample import, binding, batch preview/approval/execution, explicit recovery,
  results/reporting, orphan/broken-link diagnostics and navigation to the existing Workflow Builder/Execution Workspace.

## Compatibility

- v0.4 project schema v9 migrates forward through the accepted v0.5 migration chain to schema v15.
- Worker Protocol v2, eight native plugin identities, twelve analysis pipelines and one bundled workflow template are
  retained.
- v0.3 scientific algorithms and v0.4 workflow/DAG/recovery contracts remain authoritative and are regression-tested.
- v0.5 does not add a second scheduler/runtime or a dynamic cross-sample fan-in DAG engine.

## Result package boundary

The v0.5 batch export is an integrity-verified provenance/result **manifest snapshot** plus local HTML presentation.
It references current scientific output artifacts and re-verifies their persisted size/SHA-256 at export time. It is
not presented as a self-contained data archive that copies every large input/output byte or guarantees future replay
without the referenced project data.

## Validation policy

The release closes only when one exact Iteration 088 candidate passes Linux GCC Debug/Release, Clang Debug,
GCC ASan+UBSan, retained scientific/performance gates, v9→v15 migration/recovery/E2E closure, native Windows MSVC
Debug/Release, install/CPack/extracted-package smoke and independent Gemini plus Claude final review. Historical
evidence cannot substitute for the exact candidate.

## Citation

The stable concept DOI is `10.5281/zenodo.22012037`. A version-specific v0.5.0 DOI is added only after the accepted
release source is deposited to Zenodo.
