# Iteration 098 — Hardening & OpenGenesis-BioCore v0.6.0 Release Closure

## Frozen baseline

- Baseline ref: `accepted/iteration-097`
- Exact baseline commit: `5a85bac8f36ab0bdee84b06d9f850bdb04b9e964`
- Baseline tree: `26ba7edf28a89bc735882250348faa1226f6bd43`
- Baseline is immutable.

## Scope

Iteration 098 closes v0.6.0 without adding scientific or user-facing features. It owns final release identity,
same-candidate Linux and native Windows validation, retained scientific and migration/recovery evidence,
installation/portable-package smoke, documentation, sealed source/package hashes, and independent Gemini + Claude review.

## Release invariants

1. No second cohort matrix, QC, statistics, scheduler, worker, project or batch implementation is introduced.
2. The accepted Iteration 097 cohort flow and its frozen project/analysis/snapshot identity remain unchanged.
3. Historical analyses preserve frozen groups, selected artifacts, reference identity, matrix/statistics results and provenance.
4. Missing genotypes are never silently converted to reference genotypes.
5. Incompatible-reference results are never silently merged.
6. The exact release candidate reports version `0.6.0` with no `-dev` suffix.
7. Project schema v18, Worker Protocol v2, nine native plugins, thirteen analysis pipelines and one workflow template are preserved.
8. Every final gate refers to one exact commit/tree and one SHA-256-sealed source archive.

## Required gates

- Linux GCC Debug, GCC Release, Clang Debug and GCC ASan+UBSan: exactly 359/359 CTests.
- Retained v0.4/v0.5 compatibility and migration/recovery E2E gates.
- Complete accepted v0.6 cohort contract/registry/selection/matrix/approval/snapshot/association/execution/results/workspace regressions.
- Retained variant/scientific benchmarks, pinned GIAB HG002 compatibility, and cohort scale/resource evidence.
- Native Windows MSVC Debug and Release: exactly 359/359 CTests from the sealed source.
- Clean Windows install with exact 0.6.0 Core identity, Worker Protocol v2, nine plugins, thirteen pipelines and one template.
- App-local MSVC runtime placement, project-init smoke, portable CPack ZIP generation/extraction smoke and system-DLL exclusion.
- Four-part changed-file-complete Gemini package plus exact source ZIP and SHA-256 manifest.
- Independent Gemini ACCEPT and independent Claude ACCEPT on the same final candidate.

## Definition of done

CI success alone does not freeze Iteration 098. No source edit is permitted between the final independent reviews and
freeze. Only after both reviewers ACCEPT the exact same commit may `accepted/iteration-098`, tag `v0.6.0`, the
published GitHub release, stable-ref movement and Zenodo deposition be created.
