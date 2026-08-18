# Durable Session Recovery and Quarantine Retention v1

## 1. Scope

Iteration 017 adds a conservative project-startup recovery foundation for state that survives an
unclean Core shutdown. The increment deliberately does not resume or adopt old native worker processes.
Instead, it reconciles durable project state so that a stale active Job cannot be launched as though its
previous worker session were still trustworthy.

The iteration also closes the Iteration 016 crash window in which a complete generated-output artifact
batch can commit immediately before the separate Job-progress update fails or the Core process exits.

## 2. Durable generated-output checkpoint

Every generated-output provenance row now carries `step_progress` in project schema v4. The checkpoint
is written inside the same SQLite transaction that persists all sibling ManagedFile and
`generated_artifacts` rows for a step.

```text
worker artifact events
        |
        v
Core step artifact buffer
        |
        v
one BEGIN IMMEDIATE transaction
  - sibling managed_files
  - sibling generated_artifacts
  - identical step_progress checkpoint
        |
        v
COMMIT
        |
        v
separate Job progress update
```

If the Job progress update succeeds, the Job and artifact checkpoint agree. If Core crashes or the Job
optimistic update fails after the artifact transaction commits, the durable artifact checkpoint remains
available to startup recovery.

`OutputArtifactService` requires checkpoint progress to be finite and in `[0, 1]`. Idempotent artifact
replay also requires the existing provenance checkpoint to match the requested checkpoint.

## 3. Project schema v4

Migration v4 adds:

```sql
step_progress REAL NOT NULL DEFAULT 0.0
CHECK(step_progress >= 0.0 AND step_progress <= 1.0)
```

to `generated_artifacts` and adds an index on `(job_id, step_progress)`.

For legacy v3 rows the migration backfills `step_progress` only from the Job progress that was already
persisted in the same project database. It does not infer or invent progress that was never durable.
Therefore a historical Iteration 016 crash that committed artifacts but lost a later progress update
cannot be reconstructed retroactively unless that progress was already present elsewhere.

## 4. Startup Job reconciliation

`ProjectRecoveryService` is an Application service. It depends on Application ports and services only;
it does not inspect SQLite files, filesystem paths, PIDs, or native process handles.

For each Job whose status occupies a worker slot (`preparing`, `running`, `paused`, or `cancelling`):

1. Read the latest durable generated-output checkpoint for the Job.
2. Compute `recovered_progress = max(Job.progress, checkpoint)` when a checkpoint exists.
3. Transition the Job once to `interrupted` with the recovered progress.
4. Add the Job to the best-effort partial-output cleanup set.

The recovery pass never automatically requeues or relaunches a stale Job. A previous native session is
not durable enough to prove ownership after a Core restart.

Jobs that are already `interrupted` are also included in the cleanup set so that a prior crash between
status persistence and physical cleanup can be retried idempotently. Queued and terminal Jobs are not
rewritten by startup recovery.

Failures are isolated as typed `ProjectRecoveryIssue` values by stage so one corrupt or unavailable Job
does not prevent independent Jobs from being reconciled.

## 5. Exact crash-window behavior

The integration fixture exercises the following sequence against a real project SQLite database:

```text
Job = running, progress 0.0
        |
artifact batch commits with step_progress 0.60
        |
Job progress optimistic update is forced to fail
        |
Core session ends
        |
new ProjectRecoveryService instance reads same DB
        |
latest artifact checkpoint = 0.60
        |
Job -> interrupted, progress 0.60
```

The recovery is idempotent: a second pass does not create another Job transition after the Job is
already interrupted.

## 6. Quarantine retention

`IQuarantineRetentionStore` is an Application port. `FilesystemQuarantineRetentionStore` implements the
physical retention policy in Infrastructure.

The default recovery policy uses a 30-day minimum age. Purging is intentionally restricted to:

```text
<project>/.biocore/quarantine/outputs/<job-id>/<file>
```

Only immediate Job directories and regular, non-symlink files are eligible. The adapter:

- validates the canonical project root,
- rejects a symlinked or unsafe quarantine root,
- skips symlink/non-regular Job entries,
- skips symlink/non-regular files,
- uses lexical project-relative reporting so a skipped symlink cannot cause an outside target path to
  appear in results,
- keeps files newer than the retention threshold,
- keeps future-dated files,
- canonicalizes an eligible regular file before removal and requires its parent to remain the canonical
  Job quarantine directory,
- removes a Job quarantine directory only when it is actually empty.

This iteration does not purge runtime snapshots, execution-plan snapshots, invocation snapshots, or
registered generated outputs.

## 7. Dependency direction

```text
ProjectRecoveryService
  -> JobService
  -> IManagedFileRepository
  -> OutputArtifactCleanupService
  -> IQuarantineRetentionStore
  -> Domain Job state

SqliteManagedFileRepository
  -> IManagedFileRepository

FilesystemQuarantineRetentionStore
  -> IQuarantineRetentionStore
```

Domain and Application remain independent of SQLite and filesystem APIs.

## 8. Explicit boundaries

Iteration 017 intentionally leaves the following boundaries open:

- There is not yet a public project-open composition flow (Drogon/UI is still absent). The
  `ProjectRecoveryService` startup use case is implemented and integration-tested, but it is not yet
  wired into a final application server lifecycle that does not exist.
- Native worker PID/process ownership is not persisted. A worker that somehow remains alive after an
  abnormal Core termination is not adopted or identified by this increment. Durable DB state is
  conservatively recovered to `interrupted`.
- Interrupted Jobs are not automatically resumed or relaunched.
- Pending in-memory artifact event buffers that never reached the artifact transaction cannot be
  reconstructed; their physical files are handled by the existing quarantine cleanup path.
- Runtime/execution-plan/invocation snapshots are retained and are not reconciled or purged here.
- Quarantine retention uses filesystem modification time and a fixed Application policy default; there
  is no user-facing retention setting yet.
- Explicit `fsync`/`FlushFileBuffers`, full filesystem TOCTOU elimination, native Windows/MSVC runtime
  validation, plugin trust/sandbox/resource limits, generated-output checksums, and real bioinformatics
  plugins remain deferred.

These are pre-release boundaries, not durability or security guarantees beyond the implemented scope.
