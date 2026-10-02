# OpenGenesis-BioCore v0.6.0 Validation Record

## Authoritative release policy

The v0.6.0 release decision is made against one exact Iteration 098 source candidate. Linux validation, retained
scientific/performance evidence, v0.4/v0.5 compatibility, v0.6 cohort migration/recovery/E2E, native Windows
validation, installation/package smoke, independent Gemini review and independent Claude review must all refer
to the same candidate commit and SHA-256-sealed source archive. Historical evidence cannot close a newer source.

## Frozen predecessor

Iteration 097 is accepted and frozen at:

`5a85bac8f36ab0bdee84b06d9f850bdb04b9e964`

Its same-candidate run 37056607724 passed 359/359 in GCC Debug, GCC Release, Clang Debug and GCC ASan+UBSan,
plus the cohort scale-evidence gate. Gemini returned ACCEPT with 100% confidence and no findings.

## Iteration 098 final gates

The exact release candidate must pass:

- Linux GCC Debug, GCC Release, Clang Debug and GCC ASan+UBSan: 359/359 each;
- exact final `0.6.0` Core identity and release metadata;
- retained v0.4/v0.5 compatibility, migration/recovery and scientific/performance gates;
- complete accepted v0.6 cohort regression set and project schema v18;
- pinned GIAB HG002 compatibility and retained cohort scale/resource gate;
- exactly thirteen analysis pipelines, one workflow template and nine native plugin identities;
- native Windows MSVC Debug and Release: 359/359 each;
- clean Windows install, Core/Worker/plugin smoke and project initialization;
- app-local MSVC runtime placement and no System32/SysWOW64 or core Windows system-DLL payload leakage;
- portable `OpenGenesis-BioCore-0.6.0-windows-x64.zip` generation, extraction smoke and SHA-256 evidence;
- exactly four Markdown source-review parts plus the sealed source ZIP and checksum manifest;
- independent Gemini ACCEPT and independent Claude ACCEPT on the same exact candidate.

## Freeze and tag rule

No source edit is permitted between final independent validation and freeze. `accepted/iteration-098` and tag
`v0.6.0` must point to the exact independently accepted Iteration 098 SHA. Release publication, stable-ref movement
and version-specific Zenodo deposition remain blocked until both reviews close the same source identity.

## Compatibility identity

Project schema v18, Worker Protocol v2, nine native plugins, thirteen analysis pipelines, one workflow template,
localhost-only security, the accepted v0.3 scientific methods, v0.4 Workflow Engine 2.0 and v0.5 Project & Sample
Workspace contracts remain preserved. v0.6 adds project-scoped cohort registry, frozen analysis snapshots,
canonical matrix/QC/association execution, recovery lineage and results/report integration without a parallel engine.
