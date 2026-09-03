# Iteration 054 — v0.2.0 Final Release Closure

## Scope

Iteration 054 adds no new scientific feature. It converts the accepted v0.2 development line into one exact
release candidate and binds Linux validation, native Windows validation, installation/package smoke and
independent review to that same source identity.

## Release invariants

- Final executable/version identity is exactly `0.2.0` with no development suffix.
- Project database schema remains v8.
- Worker Protocol remains v2.
- Execution Plan schema remains v4.
- Plugin Protocol remains v2.
- Bundled plugin IDs/versions, 12 pipeline IDs/versions, scientific thresholds and output formats are unchanged.
- Retry continues to reuse the immutable execution plan and exact plugin bindings.
- Localhost/session/path-containment/process-tree/managed-file integrity boundaries remain fail-closed.
- Test floor remains exactly the active 75-test suite; final closure does not delete tests to pass release gates.

## Closure evidence

The final GitHub Actions run must create an exact source archive from the candidate commit, record its SHA-256,
and use that archive as the native Windows build source. Required gates are:

- Linux GCC Debug: 75/75.
- Linux GCC Release: 75/75.
- Linux Clang Debug: 75/75.
- Linux GCC ASan+UBSan: 75/75.
- Exact Linux release identity `0.2.0`.
- Native Windows MSVC Debug: 75/75.
- Native Windows MSVC Release: 75/75.
- Windows clean install, 8 native plugins, 12 pipelines, app-local MSVC runtimes, no System32/SysWOW64 payload.
- Windows `--init-project` smoke.
- CPack `OpenGenesis-BioCore-0.2.0-windows-x64.zip` and extracted-package smoke.
- Source and portable package SHA-256 evidence.
- Exactly four Markdown Gemini review parts generated only after all prerequisite gates pass.

Iteration 054 may be frozen only after independent Gemini returns exact `VERDICT: ACCEPT` for the exact candidate.
