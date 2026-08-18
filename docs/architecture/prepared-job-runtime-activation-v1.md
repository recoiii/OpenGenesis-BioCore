# Prepared Job Submission and Runtime Activation v1

## 1. Purpose

Iteration 020 closes the gap between authenticated REST Job creation and the already verified pipeline/worker runtime. A Job must not become scheduler-visible as runnable until OpenGenesis-BioCore has resolved the requested pipeline, resolved its plugin modules and bindings, published an immutable execution-plan snapshot, and durably associated that snapshot with the queued Job.

The iteration also activates `JobScheduler` and `WorkerRuntime` in the final local-server composition root.

## 2. Prepared Job invariant

The externally visible submission sequence is:

```text
pipeline id + exact version
  -> catalog lookup
  -> in-memory draft Job r0
  -> execution-plan snapshot for launch revision r2
  -> local draft -> queued transition (r1)
  -> BEGIN IMMEDIATE
       INSERT jobs(... queued, revision=1)
       INSERT job_execution_plans(... launch_revision=2, snapshot path)
     COMMIT
  -> HTTP 201 queued
```

A queued Job is therefore considered runnable only when `IPreparedJobStore::find_execution(job_id)` returns an association whose `launch_revision == queued_job.revision() + 1`.

The scheduler checks that association before transitioning `queued -> preparing`. The transition produces revision r2, which must match the persisted plan's launch revision and the revision embedded in the immutable execution-plan snapshot. A missing or mismatched association causes the scheduler to skip the Job without mutating it.

## 3. Schema v6

Project schema v6 adds `job_execution_plans` with a one-to-one foreign-key relationship to `jobs`.

Stored fields are:

- Job ID,
- launch revision,
- pipeline ID,
- pipeline version,
- immutable execution-plan path,
- preparation timestamp.

`SqlitePreparedJobStore::add_prepared_job()` writes the queued Job and association in one `BEGIN IMMEDIATE` transaction. If the plan association fails, the Job insert is rolled back. A failed transaction must release its SQLite write lock; this is independently tested from a second connection.

Schema-v6 INSERT/UPDATE triggers independently require the associated Job to be queued, require `launch_revision == jobs.revision + 1`, and require exact pipeline ID/version equality. These triggers provide defense-in-depth if Application validation is bypassed.

## 4. Snapshot publication boundary

Execution-plan JSON is published before the database transaction because SQLite cannot atomically commit a filesystem rename. If database activation fails, `JobSubmissionService` asks `IExecutionPlanStore::discard()` to remove the unpublished snapshot.

A process crash in the narrow interval after snapshot publication but before database activation can leave an orphan snapshot. This is safe from execution because no queued Job/association exists for the scheduler to launch. Startup orphan-snapshot reconciliation remains deferred.

Conversely, a crash after the database transaction commits but before the HTTP 201 reaches the browser may leave a valid queued Job that the runtime can execute even though the client did not observe success. Request idempotency keys are not implemented yet; clients must not assume retry is exactly-once.

## 5. Pipeline catalog

`FilesystemPipelineCatalog` scans only direct, non-symlink `.json` files beneath one canonical pipeline root and loads each file through the existing strict pipeline-definition loader.

Resolution is by exact `(pipeline_id, pipeline_version)`. Duplicate ID/version candidates reject all conflicting candidates. REST callers never provide arbitrary pipeline filesystem paths.

The catalog is refreshed during server startup and is immutable for that server session in this iteration. Hot reload is deferred.

## 6. Runtime activation

The local-server composition sequence is now:

```text
backend availability
  -> project validation
  -> runtime asset resolution
  -> migration connection / schema migration
  -> dedicated API SQLite connection
  -> dedicated WorkerRuntime SQLite connection
  -> startup recovery
  -> pipeline catalog refresh
  -> plugin registry refresh
  -> submission/preparation services
  -> scheduler + native worker supervisor + WorkerRuntime
  -> bootstrap token + LocalApiController
  -> WorkerRuntime.start()
  -> local web server.run()
  -> WorkerRuntime.stop() on server return
```

Runtime activation occurs before the listener is entered, so once the local server can accept requests it can also consume already prepared queued Jobs.

## 7. SQLite concurrency ownership

Iteration 020 activates a background runtime thread while REST requests access the same project database. Sharing one SQLite connection across those execution contexts would allow multi-statement transactions to interleave even with `SQLITE_OPEN_FULLMUTEX`.

The final composition root therefore uses:

- a short-lived migration connection,
- one API/presentation/submission connection,
- one WorkerRuntime/scheduler/artifact-ingestion connection.

Both long-lived connections use WAL and the existing busy timeout. The HTTP adapter is intentionally configured with one worker thread in Core 0.1, so the API connection has a single request execution owner. A bootstrap stress fixture submits six prepared Jobs while WorkerRuntime concurrently reads/writes through its separate connection and verifies that every Job completes.

Future HTTP connection pooling or multiple request workers must introduce explicit per-request database connection ownership rather than sharing the current API connection.

## 8. REST submission boundary

`POST /api/v1/jobs` requires an exact `pipelineId` and `pipelineVersion` and delegates to `IJobSubmitter`. It does not directly call the old draft-oriented `JobService::create()` path.

The v1 HTTP body currently exposes only:

- optional analysis ID,
- pipeline ID,
- pipeline version,
- priority.

Typed `PipelineRunBindings` are not exposed over REST yet. Pipelines requiring user parameters or managed-file bindings therefore fail preparation rather than creating a runnable Job with incomplete inputs.

## 9. Explicit boundaries

Iteration 020 does not claim:

- atomicity between filesystem snapshot publication and SQLite activation,
- startup cleanup of orphan execution-plan snapshots,
- exactly-once HTTP submission/retry semantics,
- REST parameter/input binding payloads,
- pipeline/plugin hot reload,
- native Drogon compile/run in the current validation container,
- multiple concurrent HTTP request threads or a SQLite connection pool,
- live WebSocket push,
- automatic resume of interrupted Jobs,
- native Windows/MSVC runtime validation,
- real bioinformatics tools.
