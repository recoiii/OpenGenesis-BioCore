# OpenGenesis-BioCore v0.4.0 Release Notes

OpenGenesis-BioCore v0.4.0 is the Workflow Engine 2.0 foundation release. It builds on the v0.3.0 canonical
small-variant analysis platform and adds reusable, durable workflow authoring/planning/state capabilities while
preserving the existing local-first runtime, scientific algorithms and provenance contracts.

## Major changes since v0.3.0

- Stable Workflow/Node/Edge domain model with deterministic DAG validation and planning.
- Typed parameter and artifact binding against exact plugin-module contracts.
- Bounded declarative conditional execution and branch-decision snapshots; no arbitrary scripting language.
- Node-level checkpoint, bounded retry and resume planning with artifact size/SHA-256 integrity verification.
- Durable SQLite workflow state, optimistic revision updates and cold-start recovery/reconciliation.
- Exact-version immutable reusable workflow templates and deterministic strict template documents.
- Browser Workflow Builder for template loading, scratch graph authoring, node/edge/parameter editing,
  server-side canonical validation and validated JSON export.
- Workflow Execution Workspace projecting persisted checkpoint state, attempts, failure evidence, branch decisions,
  checkpoint artifacts and authoritative resume actions.
- Runtime composition now wires workflow-state recovery at local-server startup.
- One bundled reusable FASTQ trim → QC workflow template while retaining all twelve v0.3 analysis pipelines.

## Compatibility

- v0.3 project schema v8 upgrades transactionally to v0.4 schema v9; existing Job state is retained.
- The twelve v0.3 analysis pipeline assets remain present and are counted separately from workflow templates.
- Existing `org.biocore.*`, `.biocore`, `share/biocore`, `X-BioCore-*` and `BioCore::*` compatibility-sensitive
  identifiers are preserved.
- Worker Protocol v2 and the established native plugin set remain unchanged.
- The retained v0.3 association, workspace/export, truth-set and pinned GIAB compatibility gates remain release
  prerequisites.

## Workflow execution claim boundary

The v0.4.0 source provides Workflow Engine 2.0 authoring, deterministic planning, durable state/recovery,
checkpoint/resume planning, reusable templates and an execution-state workspace. The Execution Workspace displays
what is pending, reusable, retryable, blocked or exhausted according to the accepted engine state.

This release does **not** introduce or claim a second independent native multi-node workflow dispatcher alongside
the existing Job/Pipeline runtime. The distinction is intentional and is part of the release contract.

## Validation policy

The release closes only when one exact Iteration 078 candidate passes Linux GCC Debug/Release, Clang Debug,
GCC ASan+UBSan, v0.3 compatibility and retained scientific/performance gates, native Windows MSVC Debug/Release,
install/package smoke, Workflow Engine 2.0 E2E recovery validation and independent Gemini plus Claude Code final
review. Historical evidence cannot substitute for the exact candidate.

## Citation

The stable concept DOI is `10.5281/zenodo.22012037`. A version-specific v0.4.0 DOI is added only after the accepted
release source is deposited to Zenodo.
