# Job Repository v1

## Purpose

Iteration 006 establishes the first Application-owned job persistence boundary for OpenGenesis-BioCore. It does not schedule work or start worker processes. It only defines a valid job aggregate, deterministic use-case orchestration, and project-local SQLite persistence.

## Domain model

`biocore::domain::Job` owns the persisted job invariants:

- non-blank, bounded identifiers and metadata,
- finite progress in `[0, 1]`,
- valid `JobStatus` transitions through `can_transition`,
- completed progress equal to `1.0`,
- start timestamps for jobs that have begun execution,
- finish timestamps and no active step for terminal jobs,
- non-negative, monotonically incremented revisions.

`JobPriority` contains `low`, `normal`, and `high`. String parsing and serialization for both priority and status remain in Domain so SQLite adapters do not duplicate enum vocabularies.

## Application boundary

`IJobRepository` exposes four operations:

1. insert a job while reporting identifier collisions,
2. find a job by identifier,
3. list jobs in repository-defined stable order,
4. persist mutable runtime state with optimistic concurrency.

`JobService` composes `IJobRepository`, `IIdGenerator`, and `IUtcClock`.

Creation:

```text
UTC timestamp
  -> generate identifier
  -> construct draft Job revision 0
  -> repository add
  -> retry identifier collision up to 8 times
```

Transition:

```text
find current Job
  -> validate Domain transition before clock side effects
  -> obtain UTC transition timestamp
  -> mutate Job through Job::transition_to
  -> update runtime state where stored revision == expected revision
```

A failed optimistic update is reported as an Application-level concurrent-update error. Application contains no SQLite dependency.

## Project schema version 2

Project migration version 2 adds the following nullable job metadata:

- `analysis_id`,
- `pipeline_id`,
- `pipeline_version`,
- `started_at_utc`,
- `finished_at_utc`.

It also adds a non-negative `revision` column and an ordering index on `(created_at_utc, id)`.

Existing version-1 jobs are normalized transactionally:

- started execution states receive `updated_at_utc` as a conservative start timestamp,
- terminal states receive `updated_at_utc` as a finish timestamp,
- terminal active-step values are cleared,
- completed progress is normalized to `1.0`.

If any version-2 statement fails, the entire migration rolls back and version 1 remains authoritative.

## SQLite adapter

`SqliteJobRepository`:

- uses prepared statements for all user-controlled values,
- preserves immutable creation, priority, analysis, and pipeline metadata during runtime updates,
- stores nullable values as SQL `NULL`,
- converts unsupported or domain-invalid rows into `SQLITE_MISMATCH`,
- orders lists by creation timestamp then identifier,
- updates only when the stored revision matches,
- requires the supplied revision to advance by exactly one.

## Explicit boundaries

Iteration 006 does not implement:

- job queue policy,
- resource allocation,
- worker launch or supervision,
- pause/resume process control,
- job deletion or archival,
- analysis or pipeline repositories,
- event publication or WebSocket progress,
- crash recovery for running workers.

These responsibilities remain separate from the repository foundation.
