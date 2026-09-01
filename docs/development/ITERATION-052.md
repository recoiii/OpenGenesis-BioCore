# Iteration 052 — Frontend Operational Visibility

## Goal
Expose the durable operational contracts already present in the local API without changing scientific algorithms, persistence schema, worker protocol, or execution semantics.

## Scope
- Preserve `0.2.0-dev`, project schema v8, Worker Protocol v2, Pipeline Definition schema v2, and Execution Plan schema v4.
- Preserve all Iteration 045–051 accepted invariants.
- Normalize and display exact job `attemptNumber` and `revision` in the browser.
- Expose interrupted-job retry through the existing `POST /api/v1/jobs/{id}/retry` contract; no automatic retry.
- Expose the Iteration 051 `export-manifest.json` endpoint in the selected-job UI.
- Add an explicit on-demand export/integrity verification action. Do not hash artifacts automatically on every list refresh.
- Render verified artifact counts and per-artifact integrity state from the normalized export manifest without exposing internal absolute paths.
- Invalidate browser-side export verification state after an explicit retry.
- Update the browser development identity text from the legacy 0.1 label to the 0.2 development line.
- Add a dedicated frontend operational-visibility contract test and raise the active test floor to at least 74.

## Non-goals
- No database migration.
- No new worker protocol messages.
- No scientific algorithm or threshold changes.
- No ZIP/TAR export packaging.
- No automatic retry or hidden background integrity scans.
- No change to localhost authentication, origin checks, browser-session isolation, or artifact download verification.

## Acceptance
- Linux GCC Debug, GCC Release, Clang Debug, and GCC ASan+UBSan all pass the full regression suite with at least 74 tests.
- Browser JavaScript syntax and operational helper contracts pass independently in CI.
- Existing failure-evidence, pipeline-version pin, development identity, managed-file integrity, retry, and export/report contracts remain green.
- Gemini review package contains exactly four Markdown parts and verifies its SHA-256 manifest.
- Freeze only after independent Gemini `ACCEPT`.
