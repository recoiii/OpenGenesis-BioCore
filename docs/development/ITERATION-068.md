# Iteration 068 — Release Candidate & v0.3.0 Closure

## Frozen baseline

- Baseline ref: `accepted/iteration-067`
- Exact baseline SHA: `a99aba9eb3d0ff280ce9babba4ed9106402dc1af`
- Baseline is immutable.

## Objective

Close OpenGenesis-BioCore v0.3.0 against one exact release candidate without adding new scientific behavior.
Iteration 068 is a release-hardening and evidence iteration: final release identity, cross-platform regression,
portable-package validation, retained benchmark/truth-set gates, release metadata, and independent final review.

## Release invariants

1. The supported CMake presets build exact human-facing version `0.3.0`, not `0.3.0-dev`.
2. Linux GCC Debug, GCC Release and Clang Debug each expose exactly 125 CTests and pass 125/125.
3. GCC ASan+UBSan exposes exactly 125 CTests and passes 125/125 with leak/error halting enabled.
4. The exact sealed source candidate is the only source used by native Windows closure.
5. Native Windows MSVC Debug and Release each pass exactly 125/125 CTests from that sealed source.
6. Windows clean install, Core/Worker smoke, eight native plugins, twelve pipelines, app-local runtime DLLs,
   project initialization, CPack portable ZIP extraction and system-DLL exclusion all pass.
7. The 1M-observation association benchmark, 100k workspace benchmark, 100k streaming-export benchmark,
   250k synthetic truth-set benchmark and pinned GIAB HG002 compatibility gate remain green.
8. Release metadata and documentation identify v0.3.0 consistently and do not invent a version-specific DOI.
9. The Gemini review package contains exactly four Markdown parts and embeds every 067→068 changed file exactly once.
10. `accepted/iteration-068` and the `v0.3.0` tag may be created only after independent Gemini ACCEPT and final
    Claude Code repository-level closure on the exact same candidate SHA.

## Scientific-claim boundary

Release closure must describe implemented behavior, not roadmap aspiration. In particular:

- canonical small-variant, association, export and truth-set contracts from accepted iterations remain unchanged;
- the current Annotation 2.0 implementation provides exact allele lookup and GFF/GTF feature overlap;
- transcript/CDS/codon/protein consequence prediction is not claimed by this release;
- the GIAB HG002 gate is a pinned truth-set ingestion/comparison compatibility self-check, not an end-to-end
  sequencing-read caller-accuracy claim;
- structural variants/CNV, tumor-normal somatic calling, full GWAS, ACMG classification and cloud execution remain excluded.

## Definition of done

Iteration 068 is eligible for freeze only when all CI gates above pass on one exact SHA and Gemini returns ACCEPT.
Release tagging remains blocked until Claude Code independently validates that same SHA at repository level.
