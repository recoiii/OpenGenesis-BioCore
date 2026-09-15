# OpenGenesis-BioCore v0.3.0 Validation Record

## Authoritative release policy

The v0.3.0 release decision is made against one exact Iteration 068 source candidate. Linux source validation,
native Windows validation, installation/package smoke, retained scientific benchmark gates, independent Gemini
review and final Claude Code repository-level closure must all refer to the same candidate SHA/source archive.
Historical evidence cannot close a newer source.

## Frozen predecessor

Iteration 067 is accepted and frozen at:

`a99aba9eb3d0ff280ce9babba4ed9106402dc1af`

Its Linux matrix passed 125/125 in GCC Debug, GCC Release, Clang Debug and GCC ASan+UBSan. Its 250k synthetic
truth-set benchmark and pinned GIAB HG002 GRCh38 v4.2.1 chr20 compatibility gate also passed before Gemini returned
`ACCEPT` with 100% confidence and no findings.

## Iteration 068 final gates

The exact release candidate must pass:

- Linux GCC Debug 125/125;
- Linux GCC Release 125/125;
- Linux Clang Debug 125/125;
- Linux GCC ASan+UBSan 125/125;
- exact final `0.3.0` Core identity under supported presets;
- 1M-observation case/control regression benchmark;
- 100k-variant workspace bounded-query benchmark;
- 100k-variant streaming export benchmark;
- 250k-variant synthetic truth-set benchmark with invariant 245,000 TP / 5,000 FP / 5,000 FN;
- pinned external GIAB HG002 GRCh38 v4.2.1 chr20 fixture identity and compatibility gate;
- native Windows MSVC Debug 125/125;
- native Windows MSVC Release 125/125;
- clean Windows Release install;
- exactly eight installed native plugins and twelve installed pipelines;
- app-local MSVC runtime placement beside Core and native plugins;
- no System32/SysWOW64 or core Windows system-DLL payload leakage;
- installed and extracted-package `--init-project` smoke;
- portable `OpenGenesis-BioCore-0.3.0-windows-x64.zip` generation and SHA-256 evidence;
- exactly four Markdown parts for independent Gemini review after every prerequisite CI gate succeeds;
- Gemini `ACCEPT` on the exact candidate;
- independent Claude Code repository-level closure on that same candidate before the release tag is created.

## Release identity and tag rule

The source candidate is release-ready only after all final gates close. `accepted/iteration-068` is immutable once
created after Gemini ACCEPT. The `v0.3.0` tag must point to the exact accepted Iteration 068 SHA and must not be
created until the separate Claude Code closure is also successful. No source edits are permitted between final
independent validation and tagging.
