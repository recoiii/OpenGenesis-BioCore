# Iteration 085 — Results & QC Aggregation

Status: CANDIDATE, awaiting independent Gemini review. Not accepted or frozen.
Baseline: `accepted/iteration-084` / `e4335934a1872454f7f0a7519ed3c96a3ddc95f4`.
Development branch: `v0.5.0-dev`.
Development identity: `0.5.0-dev`.

## Objective

Provide a sample-level batch results view that aggregates durable status, current output links and
existing QC metrics across Iteration 084 attempt lineage, while exposing only scientifically
compatible terminal VCF outputs to the existing v0.3 multi-sample matrix engine.

## Deliverables

- lineage-aware current output resolution across partial resume attempts;
- hard retry boundary that prevents stale parent output inheritance;
- node-scope shadowing while a resume attempt recomputes an output;
- bounded checksum-verified text artifact reader;
- sample-level latest Job/attempt state and verified output links;
- parsing of existing FASTA/FASTQ/alignment/VCF-QC metrics only;
- explicit `comparable`, `incomplete` and `incompatible` QC comparison states;
- exact plugin/schema/parameter/reference comparison contracts;
- no zero imputation for missing/null metrics;
- guarded terminal single-sample VCF -> existing v0.3 ingestion/matrix bridge;
- sample-name, terminal-output and production-contract guards before matrix aggregation;
- dedicated result/QC/lineage/matrix and filesystem integrity tests;
- retained Iterations 079–084 and v0.4 regression coverage.

## Acceptance gates

1. Sample result status is derived from the latest durable attempt while output links follow only
   the valid current lineage.
2. Resume may inherit valid untouched parent outputs, but a full retry cannot inherit artifacts from
   the prior attempt chain.
3. A node selected for recomputation shadows its parent outputs immediately; stale files cannot be
   presented as current while replacement work is pending.
4. QC values come only from existing plugin outputs; absent metrics remain absent and JSON `null`
   remains null rather than becoming zero.
5. QC comparison is blocked/flagged when plugin version, metric schema, frozen parameters, reference
   identity or metric shape is incompatible or incomplete.
6. Every QC/VCF payload is size-bounded and checksum/integrity verified before parsing.
7. Matrix preview accepts only completed samples with exactly one current terminal VCF artifact.
8. Matrix aggregation requires a common terminal production contract and uses only the existing
   v0.3 VCF ingestion + `MultiSampleMatrix` implementation; each VCF sample name must match its
   project sample id.
9. Incompatible, missing, stale, multi-sample or otherwise nonconforming VCF outputs cannot be
   silently auto-merged.
10. Iteration 085 adds no project-schema migration, new biological/statistical score or cross-sample
    fan-in workflow execution.
11. Independent Gemini ACCEPT on the exact candidate/source SHA is mandatory before freeze.

Project owner/developer: Recep Çelik. AI-assisted architecture, implementation, validation,
debugging and review-package preparation: ChatGPT. Independent Gemini review is pending.
