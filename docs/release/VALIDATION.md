# OpenGenesis-BioCore v0.5.0 Validation Record

## Authoritative release policy

The v0.5.0 release decision is made against one exact Iteration 088 source candidate. Linux validation, retained
scientific/performance evidence, v0.4 compatibility, v0.5 migration/recovery/project-workspace E2E, native Windows
validation, installation/package smoke, independent Gemini review and independent Claude review must all refer to
the same candidate commit and SHA-256-sealed source archive. Historical evidence cannot close a newer source.

## Frozen predecessor

Iteration 087 is accepted and frozen at:

`1e18fc1632042d272f51995fb25370861c250707`

Its Linux matrix passed 288/288 in GCC Debug, GCC Release, Clang Debug and GCC ASan+UBSan. Gemini returned exact
`VERDICT: ACCEPT` with no blocking or non-blocking findings.

## Iteration 088 final gates

The exact release candidate must pass:

- Linux GCC Debug 288/288;
- Linux GCC Release 288/288;
- Linux Clang Debug 288/288;
- Linux GCC ASan+UBSan 288/288 with leak/error halting enabled;
- exact final `0.5.0` Core identity and release metadata;
- retained v0.4/v0.3 compatibility and scientific/performance gates;
- accepted v0.5 migration/rollback gates across project metadata, sample registry, binding, batch planning/execution;
- Iteration 084 recovery restart/resume/retry/integrity gates;
- Iteration 087 persisted Project Workspace E2E gate;
- v0.4 schema-v9 projects migrate through the accepted chain to current schema v15;
- exactly twelve analysis pipelines, one workflow template and eight native plugin identities remain installed;
- native Windows MSVC Debug 288/288;
- native Windows MSVC Release 288/288;
- clean Windows Release install, Core/Worker/plugin smoke and project initialization;
- app-local MSVC runtime placement and no System32/SysWOW64 or core Windows system-DLL payload leakage;
- portable `OpenGenesis-BioCore-0.5.0-windows-x64.zip` generation, extraction smoke and SHA-256 evidence;
- exactly four Markdown source-review parts generated only after the prerequisite CI/evidence gates pass;
- independent Gemini `VERDICT: ACCEPT` on the exact candidate;
- independent Claude `ACCEPT` on the same exact candidate before freeze/tag/release.

## Freeze and tag rule

No source edit is permitted between final independent validation and freeze. `accepted/iteration-088` and the
`v0.5.0` tag must both point to the exact independently accepted Iteration 088 SHA. Both remain blocked until Gemini
and Claude accept the same source identity.

## Compatibility identity

Project schema v15 is the v0.5 current schema reached from the v0.4 schema-v9 baseline by the accepted migration
chain. Worker Protocol v2, eight native plugin identities, twelve analysis pipelines, one workflow template,
localhost-only security model, v0.3 scientific methods and v0.4 workflow contracts remain preserved.
