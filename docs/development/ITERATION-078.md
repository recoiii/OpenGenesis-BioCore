# Iteration 078 — Workflow Engine 2.0 Hardening & v0.4.0 Closure

## Frozen baseline

- Baseline ref: `accepted/iteration-077`
- Exact baseline SHA: `d0dc0e41da7c55e128a718020a1d03da0687662e`
- Baseline is immutable.

## Objective

Close OpenGenesis-BioCore v0.4.0 against one exact source candidate without adding new scientific or workflow-engine
features. Iteration 078 is a release-hardening and evidence iteration: exact release identity, retained v0.3
compatibility/scientific gates, Workflow Engine 2.0 end-to-end recovery evidence, native Windows validation,
portable packaging, release metadata, and independent final review.

## Release invariants

1. Supported release presets report exact human-facing version `0.4.0`.
2. Linux GCC Debug, GCC Release and Clang Debug each expose exactly 224 CTests and pass 224/224.
3. GCC ASan+UBSan exposes exactly 224 CTests and passes 224/224 with leak/error halting enabled.
4. The exact SHA-256-sealed source archive is the only source used for native Windows closure.
5. Native Windows MSVC Debug and Release each pass exactly 224/224 CTests from that sealed source.
6. Windows clean install, Core/Worker smoke, eight native plugins, twelve legacy analysis pipelines, one bundled
   workflow template, app-local runtime DLLs, project initialization, CPack ZIP extraction and system-DLL exclusion
   all pass.
7. A representative v0.3 project database at schema v8 upgrades transactionally to schema v9 without losing retained
   Job state, and the v0.3 twelve-pipeline asset set remains present unchanged.
8. The Workflow Engine 2.0 E2E gate covers canonical workflow decoding, durable SQLite state, checkpoint artifact
   integrity verification, process reopen/recovery and deterministic `reuse_completed` / `execute` projection.
9. The retained v0.3 scientific/performance evidence remains green: association, workspace, export, synthetic truth
   set and pinned GIAB HG002 compatibility gates.
10. Release metadata and documentation identify v0.4.0 consistently and do not fabricate a version-specific DOI.
11. The Gemini review package contains exactly four Markdown parts and embeds every 077→078 changed file exactly once.
12. `accepted/iteration-078` may be created only after independent Gemini ACCEPT. The `v0.4.0` tag remains blocked
   until a final Claude Code repository-level closure validates the exact same accepted SHA.

## Compatibility and claim boundary

- v0.4 preserves the accepted v0.3 scientific algorithms, thresholds and compatibility-sensitive machine identifiers.
- Project schema advances from v8 to v9 only to add Workflow Engine 2.0 durable state tables and constraints.
- Existing twelve analysis pipeline assets remain; the bundled workflow template is a distinct
  `*.workflow-template.json` asset and must not be counted as a thirteenth analysis pipeline.
- Workflow Engine 2.0 adds authoring, DAG planning, binding, bounded branching, resume planning, durable workflow state,
  reusable templates, builder UI and execution-state workspace.
- v0.4 does **not** claim a new second native multi-node dispatcher independent of the existing Job/Pipeline runtime.
- Structural variants/CNV, tumor-normal somatic calling, full GWAS, ACMG classification and cloud/distributed
  execution remain outside scope.

## Definition of done

Iteration 078 is eligible for freeze only when every exact-candidate CI gate above passes and Gemini returns ACCEPT.
Release tagging remains blocked until Claude Code independently validates the same frozen SHA.
