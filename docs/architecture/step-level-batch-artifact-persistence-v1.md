# Step-Level Batch Artifact Persistence and Partial Output Quarantine v1

Iteration 016 closes the multi-output persistence boundary accepted in Iteration 015. Successful
outputs from one pipeline step are buffered in Core and registered as one repository batch before the
step progress event is persisted. Failed or interrupted jobs trigger best-effort quarantine of
unregistered OpenGenesis-BioCore-owned output files.

## 1. Architectural boundary

The worker still never writes SQLite. It emits bounded Worker Protocol v2 `artifact` events for all
validated outputs of a successful step and then emits that step's `progress` event. Core treats the
progress event as the batch-commit boundary.

```text
worker plugin step
  -> validate every declared output
  -> artifact(step, port A)
  -> artifact(step, port B)
  -> progress(active_step_id = step)

Core WorkerEventIngestionSession
  -> buffer artifact A
  -> buffer artifact B
  -> on matching progress:
       OutputArtifactService::register_generated_outputs_batch
         -> inspect every output again
         -> IManagedFileRepository::add_generated_outputs_batch
            -> one SQLite BEGIN IMMEDIATE transaction
            -> all ManagedFile + generated_artifacts rows
            -> COMMIT or rollback all
       -> persist Job progress only after batch registration succeeds
```

No Worker Protocol version change is required because Iteration 015 Protocol v2 already defines the
artifact event and the worker already emits the step progress event after its artifact series.

## 2. Application batch contract

`OutputArtifactService::register_generated_outputs_batch` accepts at most 256 artifacts and requires
all requests to share one job, step, plugin ID, plugin version, and module ID. Output ports and relative
paths must be unique. Every file is independently inspected before repository persistence.

Idempotency is step-wide:

- none registered -> validate and attempt one batch write;
- all registered with exact provenance -> return the existing batch in request order;
- only a subset registered -> reject as a partial-persistence conflict;
- any provenance mismatch -> reject;
- an output path owned by another ManagedFile -> reject.

This prevents a replay from silently converting a partial legacy state into a complete step.

## 3. SQLite all-or-none transaction

`SqliteManagedFileRepository::add_generated_outputs_batch` validates that all artifacts belong to one
step and opens one `BEGIN IMMEDIATE` transaction. For every sibling artifact it inserts the ManagedFile
row and the generated-output provenance row. A unique, foreign-key, check, or ManagedFile conflict
returns failure and the RAII transaction rolls back every prior sibling insertion. Only after every pair
has succeeded is the transaction committed.

The single-artifact repository API delegates to the same batch implementation with a one-element span.

## 4. Progress as the Core commit boundary

`WorkerEventIngestionSession` buffers artifact DTOs in memory. Artifact events from different steps or
modules may not be interleaved, and duplicate output ports or paths are rejected.

When a `progress` event arrives:

1. it must identify the same active step as the pending artifact batch;
2. the whole artifact batch is registered;
3. the pending buffer is cleared only after successful registration;
4. Job progress is persisted after the batch registration succeeds.

If artifact persistence throws, progress is not persisted and the event sequence is not advanced. The
pending batch remains available for the same in-process session to retry.

The artifact transaction and Job-progress update are deliberately separate repository transactions. A
Core crash after the artifact batch commits but before progress commits can therefore leave a complete,
registered step batch with stale Job progress. Replayed registration is idempotent, but durable crash
session reconstruction is still deferred.

## 5. Partial output quarantine

Deletion is intentionally not used. `OutputArtifactCleanupService` asks the ManagedFile repository for
all registered artifacts of a job and passes those relative paths as protected files to the
`IPartialOutputCleaner` port.

`FilesystemPartialOutputCleaner` scans only the canonical project `outputs/` directory. A cleanup
candidate must:

- have a filename beginning with `<job-id>--`;
- not be a registered/protected generated artifact;
- be a regular non-symlink file;
- canonicalize exactly beneath the flat `outputs/` directory.

Eligible files are moved with filesystem `rename` to:

```text
.biocore/quarantine/outputs/<job-id>/<original-name>.partial[.N]
```

Reserved quarantine directories must be non-symlink directories and remain project-local. Existing
registered outputs, outputs from another job, symlinks, and non-regular entries are never followed or
removed. Suspicious entries are reported as skipped.

Cleanup is best-effort: failures are surfaced as cleanup diagnostics and do not rewrite the already
persisted terminal Job status.

## 6. Failure and interruption integration

Cleanup runs after a worker `failed` event, after an unexpected native process exit transitions a job to
`interrupted`, and after heartbeat timeout persistence in `WorkerRuntime`. Pending in-memory artifact
batches are discarded before cleanup. Registered artifacts from earlier successful steps are protected.

The deterministic demo failure module writes a real partial output and exits 3. The integration test
proves that the file is not registered and is moved out of `outputs/` into quarantine.

## 7. Multi-output execution fixture

`org.biocore.demo.multi` declares `left` and `right` text outputs. It writes both files successfully.
The real worker emits two artifact events followed by step progress; Core buffers both and persists them
as one generated-output batch. This fixture exists only to validate the execution/persistence contract;
it is not a bioinformatics analysis.

## 8. Explicit boundaries

Iteration 016 does not claim:

- durable recovery of in-memory pending artifact batches after a Core crash;
- one cross-repository transaction combining artifact rows and Job progress;
- startup reconciliation of abandoned files after an unclean Core shutdown;
- deletion/retention policy for quarantine contents;
- cleanup of arbitrary plugin-created files that do not use OpenGenesis-BioCore's job-prefixed output namespace;
- full filesystem handle/path TOCTOU elimination;
- explicit `fsync` / `FlushFileBuffers` durability;
- native Windows/MSVC runtime validation;
- plugin signing, trust, sandboxing, or CPU/RAM/disk limits;
- real bioinformatics plugins.

These remain pre-release boundaries.
