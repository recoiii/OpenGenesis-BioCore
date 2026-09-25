# OpenGenesis-BioCore v0.4.0 Validation Record

## Authoritative release policy

The v0.4.0 release decision is made against one exact Iteration 078 source candidate. Linux source validation,
retained v0.3 scientific/performance evidence, v0.3 compatibility gates, Workflow Engine 2.0 E2E recovery validation,
native Windows validation, installation/package smoke, independent Gemini review and final Claude Code
repository-level closure must all refer to the same candidate SHA/source archive. Historical evidence cannot close
a newer source.

## Frozen predecessor

Iteration 077 is accepted and frozen at:

`d0dc0e41da7c55e128a718020a1d03da0687662e`

Its Linux matrix passed 220/220 in GCC Debug, GCC Release, Clang Debug and GCC ASan+UBSan. Gemini returned
`ACCEPT` with 100% confidence and no findings.

## Iteration 078 final gates

The exact release candidate must pass:

- Linux GCC Debug 224/224;
- Linux GCC Release 224/224;
- Linux Clang Debug 224/224;
- Linux GCC ASan+UBSan 224/224;
- exact final `0.4.0` Core identity;
- v0.3 schema-v8 → schema-v9 migration compatibility with retained Job state;
- exact retained twelve v0.3 analysis pipeline assets plus one distinct workflow template;
- Workflow Engine 2.0 E2E persistent-state/reopen/artifact-integrity/resume projection gate;
- retained 1M-observation association benchmark;
- retained 100k-variant workspace bounded-query benchmark;
- retained 100k-variant streaming export benchmark;
- retained 250k-variant synthetic truth-set benchmark;
- retained pinned GIAB HG002 GRCh38 v4.2.1 chr20 compatibility gate;
- native Windows MSVC Debug 224/224;
- native Windows MSVC Release 224/224;
- clean Windows Release install;
- exactly eight installed native plugins;
- exactly twelve installed `*.biocore-pipeline.json` analysis pipelines;
- exactly one installed `*.workflow-template.json` reusable workflow template;
- app-local MSVC runtime placement beside Core and native plugins;
- no System32/SysWOW64 or core Windows system-DLL payload leakage;
- installed and extracted-package `--init-project` smoke;
- portable `OpenGenesis-BioCore-0.4.0-windows-x64.zip` generation and SHA-256 evidence;
- exactly four Markdown parts for independent Gemini review after every prerequisite CI gate succeeds;
- Gemini `ACCEPT` on the exact candidate;
- independent Claude Code repository-level closure on that same candidate before the release tag is created.

## Release identity and tag rule

The source candidate is release-ready only after all final gates close. `accepted/iteration-078` is immutable once
created after Gemini ACCEPT. The `v0.4.0` tag must point to the exact accepted Iteration 078 SHA and must not be
created until the separate Claude Code closure is also successful. No source edits are permitted between final
independent validation and tagging.

## Compatibility identity

Project schema v9 is a forward migration from the v0.3 schema-v8 baseline. Worker Protocol v2, the eight native
plugin identities, twelve legacy analysis pipeline assets, local-only browser security model and v0.3 scientific
algorithms remain preserved. The new workflow template is a separate asset type and is not a pipeline-count change.
