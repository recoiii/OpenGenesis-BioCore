# Iteration 095 — Cohort Execution, Recovery & Lineage

Baseline: `accepted/iteration-094` / `b8a9b175d38cb2b3968a4e160c1e0f650557c038`.
Status: candidate only; independent Gemini review required before freeze.

## Scope

Iteration 095 hardens the durable execution boundary around the immutable cohort analysis
snapshot accepted in Iteration 093 and the snapshot-bound association integration accepted
in Iteration 094. It reuses the existing Job submission/service/runtime abstraction rather
than creating a second scheduler.

The iteration owns durable attempt reservation, idempotent initial submission, persisted
cancellation intent, crash reconciliation, retry lineage and the rule that a cohort
analysis is not complete merely because its underlying Job exited successfully. Result
exploration/reporting remains Iteration 096; integrated HTTP/browser workspace wiring
remains Iteration 097.

## Persistent execution contract

Project schema v18 adds `cohort_analysis_attempts`, keyed by project, analysis and attempt
number and foreign-keyed to the immutable sealed analysis snapshot. Each row persists:

- stable attempt identity and monotonically increasing attempt number;
- optional parent attempt for retries;
- immutable snapshot digest;
- operation idempotency key and payload SHA-256;
- scheduler Job identity after durable handoff;
- cancellation request state;
- queued/running/completed/failed/cancelled/interrupted state;
- failure evidence;
- verified result-manifest identity and SHA-256 for completed attempts.

Attempt identity, parent lineage, snapshot digest, idempotency identity and original
creation timestamp are immutable. Attempt history cannot be deleted.

## Submission and recovery semantics

Initial submission re-verifies the exact frozen source/reference selection before any
attempt is reserved. Reservation is durable before Job handoff. The same idempotency
key plus payload digest returns the same logical attempt; reuse of the key with a
different payload conflicts; a second initial submit cannot create another logical
analysis execution.

If the process crashes after reservation but before Job attachment, recovery marks the
attempt interrupted rather than completed. If a Job disappears, the attempt is likewise
interrupted. A Job reaching `completed` is not sufficient for cohort completion:
completion is committed only when a verified result-manifest file ID and SHA-256 are
persisted atomically on the attempt.

Cancellation intent is persisted before a Job state change. Repeated cancellation is
idempotent. A completed attempt cannot be cancelled or retried.

## Retry and input-integrity semantics

Only the latest failed, cancelled or interrupted attempt can produce a retry child.
A retry has a new attempt ID and a new Job ID and records `parent_attempt_id`; it keeps
the exact immutable snapshot digest. Concurrent/duplicate retry requests collapse by
idempotency key. A successful completed attempt is immutable and its result manifest
cannot be overwritten.

Before retry, the pinned reference and selected VCF source identities are revalidated
through the accepted selection gate. Changed/missing bytes or lineage block retry;
the old frozen execution cannot silently consume changed input.

## Scheduler reuse boundary

The application service hands a reserved attempt to the existing `IJobSubmitter` /
`JobService` boundary using the fixed cohort pipeline identity
`org.biocore.cohort-analysis@1.0.0`. This iteration establishes and verifies the
durable handoff/recovery contract. It does not claim a new scheduler, browser/API route,
result explorer or report surface. Runtime composition of the cohort workflow into the
integrated project workspace remains Iteration 097.

## Verification gate

Dedicated integration tests cover:

- store-level initial reservation, replay and idempotency conflict;
- second-initial-submit rejection;
- changed-input rejection before reservation/handoff;
- crash after reservation but before Job attachment;
- new retry attempt/Job identity and parent lineage;
- duplicate retry replay;
- persisted/idempotent cancellation;
- prevention of false completion from Job completion alone;
- verified manifest completion and completed-attempt immutability;
- schema v17→v18 migration and DB-level immutability.

The full regression inventory is expected to be 349 CTest registrations after the eight
Iteration 095 modes are added. The four Linux lanes and cohort-scale evidence must pass
on the exact candidate. The exact four-part Gemini review package and source ZIP must
come from that same commit. No `accepted/iteration-095` ref is created before Gemini
returns ACCEPT.

Native Windows and distribution/package closure remain Iteration 098 gates.

Project owner/developer: Recep Çelik. ChatGPT contribution: AI-assisted architecture,
implementation, debugging, tests, documentation and independent-review preparation.
