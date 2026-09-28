# Iteration 088 — Hardening & OpenGenesis-BioCore v0.5.0 Release Closure

## Frozen baseline

- Baseline ref: `accepted/iteration-087`
- Exact baseline SHA: `1e18fc1632042d272f51995fb25370861c250707`
- Baseline is immutable.

## Objective

Close OpenGenesis-BioCore v0.5.0 against one exact source candidate without adding features. Iteration 088 owns
release identity, consolidated Linux/scientific/migration/recovery evidence, native Windows validation,
install/portable-package smoke, sealed source/package hashes and final independent Gemini + Claude review.

## Release invariants

1. Supported release presets and installed Core report exact `0.5.0`.
2. GCC Debug, GCC Release, Clang Debug and GCC ASan+UBSan each expose exactly 288 CTests and pass 288/288.
3. The sealed source ZIP SHA-256 is the only source used for native Windows closure.
4. MSVC Debug and Release each pass exactly 288/288 from the sealed source.
5. Clean install and extracted CPack ZIP expose eight native plugins, twelve analysis pipelines, one workflow
   template, frontend assets and exact 0.5.0 identity without system-DLL leakage.
6. Retained v0.3 science/v0.4 compatibility evidence remains green.
7. Accepted v0.5 migration and rollback suites prove the schema-v9→v15 path component-by-component; recovery and
   project-workspace E2E remain green on the exact candidate.
8. Release metadata consistently identifies v0.5.0 and keeps only the stable concept DOI until a version DOI exists.
9. No new biological algorithm, scheduler/runtime, project feature or schema migration is introduced in 088.
10. The Gemini package contains exactly four Markdown parts and every 087→088 changed file exactly once.
11. Gemini must return exact `VERDICT: ACCEPT` for this candidate.
12. Claude must independently return ACCEPT for the same commit/source SHA; only then may
    `accepted/iteration-088` and tag `v0.5.0` be created.

## Definition of done

Iteration 088 is not frozen after CI alone and is not frozen after only one reviewer. Both final independent reviews
must close the exact same candidate source with no source edits between them.
