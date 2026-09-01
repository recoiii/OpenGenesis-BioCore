# OpenGenesis-BioCore v0.2.0-dev — Iteration 047

## Title

Job Failure Diagnostics & Structured Failure Evidence

## Goal

Persist the reason a job reached `failed` or `interrupted` so that actionable diagnostic evidence survives browser refreshes and process restarts instead of being available only in ephemeral worker telemetry.

Iteration 047 advances the project database to schema v7. Worker Protocol remains v2.

## Intended changes

- introduce a validated `JobFailure` domain value with typed failure kinds;
- add schema-v7 failure evidence fields directly to `jobs` so terminal state and evidence are persisted atomically;
- backfill pre-v7 failed/interrupted jobs with an explicit legacy terminal-state diagnostic;
- enforce status/evidence consistency with SQLite triggers;
- preserve worker-reported failure message, non-zero exit code and worker timestamp;
- persist unexpected process exits, heartbeat timeouts and startup crash recovery as typed interruption evidence;
- clear interruption evidence when an interrupted job is deliberately re-queued;
- expose durable failure evidence through job JSON and deterministic execution reports;
- show failure evidence in the local browser detail view while retaining live logs as ephemeral telemetry;
- add a dedicated failure-diagnostics CTest and raise the active CI floor from 68 to 69.

## Explicit non-goals

Iteration 047 must not:

- change Worker Protocol version 2;
- change plugin or pipeline identifiers/versions;
- change biological algorithms, thresholds or scientific outputs;
- change loopback-only networking, browser-session isolation or token separation;
- add cloud telemetry or external reporting;
- retain an unbounded history of multiple failures per job;
- automatically infer a scientific cause from a process/runtime failure.

## Acceptance criteria

1. Worker `failed` events persist typed failure evidence atomically with the failed job transition.
2. Unexpected process exit without a terminal lifecycle event persists an interrupted process-exit diagnostic and exit code.
3. Heartbeat timeout and startup stale-job recovery persist distinct interruption kinds.
4. Schema v7 deterministically backfills existing failed/interrupted v6 rows without altering completed/cancelled/non-terminal jobs.
5. SQLite rejects terminal rows without evidence, non-terminal rows with evidence and mismatched typed evidence.
6. Re-queuing an interrupted job clears its previous failure evidence.
7. `/api/v1/jobs` and `/api/v1/jobs/{id}` expose a structured nullable `failure` object.
8. JSON/HTML execution reports preserve the terminal failure message/evidence.
9. Browser detail view presents failure evidence after refresh; live logs remain explicitly ephemeral.
10. Active CTest floor is at least 69 and GCC Debug, GCC Release, Clang Debug and GCC ASan+UBSan all pass.
11. `0.2.0-dev`, Worker Protocol v2, security boundaries and biological behavior remain unchanged.
12. Independent Gemini review returns `ACCEPT` before Iteration 047 is frozen.

## Freeze rule

Iteration 047 remains open until the exact final candidate passes the complete Linux validation matrix and independent Gemini review. Any blocking finding requires a revised candidate and a fresh exact review package.
