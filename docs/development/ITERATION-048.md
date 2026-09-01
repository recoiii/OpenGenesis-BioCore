# OpenGenesis-BioCore v0.2.0-dev — Iteration 048

## Title

Retry & Recovery Semantics

## Goal

Make interrupted-job retry explicit, atomic, durable, and auditable without changing the immutable execution-plan snapshot that was accepted at original submission time.

Iteration 048 advances the project database to schema v8. Worker Protocol remains v2.

## Intended changes

- add a durable `attempt_number` to jobs, starting at 1;
- keep `revision` as optimistic-concurrency/runtime revision rather than overloading it as a retry counter;
- allow retry only for `interrupted` jobs; `failed`, `cancelled`, and `completed` jobs remain non-retryable in this iteration;
- reset retry progress to 0, clear active/start/finish runtime state, and clear prior interruption failure evidence;
- atomically persist interrupted→queued, attempt advancement, and the next scheduler launch revision in one SQLite transaction;
- reuse the exact immutable execution-plan snapshot path and pipeline identity from the original prepared association;
- prohibit updates to execution-plan path, pipeline id/version, or original preparation timestamp with schema-v8 triggers;
- require launch revisions to advance monotonically;
- prevent generic `JobService` interrupted→queued transitions so a queued retry cannot be persisted with a stale prepared launch revision;
- expose `attemptNumber` in job JSON and add `POST /api/v1/jobs/{id}/retry` for the local API;
- preserve explicit recovery: startup recovery marks stale work interrupted but never auto-retries it;
- add a dedicated retry semantics CTest and raise the active CI floor from 69 to 70.

## Explicit non-goals

Iteration 048 must not:

- mutate or regenerate the original execution-plan snapshot during retry;
- retry `failed` jobs;
- automatically retry after process failure, heartbeat timeout, or startup recovery;
- change Worker Protocol version 2;
- change plugin or pipeline identifiers/versions;
- change biological algorithms, thresholds, or scientific outputs;
- change loopback-only networking, browser-session isolation, token separation, or process-tree cancellation ownership;
- add retry backoff policies, retry limits, or distributed/cloud scheduling.

## Acceptance criteria

1. A newly submitted prepared job has `attemptNumber == 1`.
2. Only an `interrupted` prepared job can be retried.
3. A retry increments `attemptNumber` exactly once while `revision` continues its independent monotonic runtime sequence.
4. Retry resets progress to 0 and clears active step, started/finished timestamps, and prior interruption failure evidence.
5. Job retry state and next `launch_revision` are persisted atomically; a concurrency race leaves both unchanged.
6. The execution-plan path, pipeline id/version, and original preparation timestamp are byte-for-byte unchanged across retries.
7. SQLite rejects mutation of immutable prepared-plan identity and non-monotonic launch revisions.
8. The scheduler validates both attempt identity and expected launch revision before worker launch.
9. Generic `JobService` interrupted→queued transition is rejected; retry must use `JobRetryService`.
10. `POST /api/v1/jobs/{id}/retry` returns the newly queued job and job JSON exposes `attemptNumber`.
11. Startup recovery remains interruption-only and never auto-requeues work.
12. Active CTest floor is at least 70 and GCC Debug, GCC Release, Clang Debug, and GCC ASan+UBSan all pass.
13. `0.2.0-dev`, Worker Protocol v2, security boundaries, process-tree cancellation, and biological behavior remain unchanged.
14. Independent Gemini review returns `ACCEPT` before Iteration 048 is frozen.

## Freeze rule

Iteration 048 remains open until the exact final candidate passes the complete Linux validation matrix and independent Gemini review. Any blocking finding requires a revised Iteration 048 candidate and a fresh exact four-part review package.
