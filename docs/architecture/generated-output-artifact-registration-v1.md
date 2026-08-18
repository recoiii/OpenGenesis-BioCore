# Generated Output Artifact Registration Architecture v1

Status: Iteration 015 candidate  
Date: 2026-08-07

## 1. Purpose

Iteration 015 closes the first durable OpenGenesis-BioCore plugin data loop. A successful plugin output can now be
validated by the worker, reported over the worker lifecycle transport, independently revalidated by
Core, and registered in the project database as a `ManagedFile` with
`StorageMode::generated_output` plus immutable provenance.

The worker still never writes SQLite directly.

## 2. Trust and dependency boundaries

- Domain owns `ManagedFile` and `StorageMode::generated_output` invariants.
- Application owns output-artifact registration policy through `OutputArtifactService` and the
  `IManagedFileRepository` / `IOutputArtifactInspector` ports.
- Worker Protocol owns the flat wire representation of artifact lifecycle events.
- Infrastructure owns filesystem reinspection and SQLite persistence.
- `biocore-worker` validates expected step outputs and emits artifact events; it does not persist them.
- Core ingestion maps a valid artifact event to `OutputArtifactService`.

No SQLite or filesystem dependency is introduced into Domain or Application.

## 3. Worker Protocol v2

Adding a new lifecycle `artifact` message is a wire-incompatible change, so Iteration 015 increments
`current_protocol_version` from 1 to 2 instead of silently extending v1.

An artifact lifecycle event carries only bounded flat metadata:

- job id and launch revision from the common envelope;
- sequence and worker timestamp;
- step id;
- output port;
- plugin id and version;
- module id;
- declared file type;
- project-relative output path.

The event carries no file bytes and no arbitrary absolute path. Unknown, duplicate, missing, unrelated,
or invalid fields are rejected by the strict codec.

## 4. Worker-side output publication policy

After a plugin process exits with native code zero, the worker first checks **all** declared outputs for
that step through `FilesystemOutputArtifactInspector`. Only when every expected output is present and
valid does the worker emit artifact events, one per output, followed by the step progress event.

If the plugin exits non-zero, or if any expected output is missing/unsafe, the worker emits `failed` and
no artifact event for that step. A failing plugin may physically leave partial files in `outputs/`; those
files are intentionally not registered.

## 5. Core-side independent reinspection

Core never trusts the worker's path metadata as sufficient proof. `FilesystemOutputArtifactInspector`
reopens the project boundary and requires each artifact to be:

- represented by a bounded normalized project-relative path;
- directly inside the flat `outputs/` namespace;
- an existing regular file;
- not a symbolic link;
- canonical at the expected exact path;
- representable within the supported signed file-size range.

The inspector returns display name, canonical managed path, relative path, and size. Core uses these
values to create the `ManagedFile` record.

## 6. Persistence model

Project schema version 3 introduces `generated_artifacts`.

Each provenance row stores:

- `managed_file_id` -> `managed_files(id)`;
- job id -> `jobs(id)`;
- step id;
- output port;
- plugin id and version;
- module id;
- file type;
- project-relative path;
- registration timestamp.

`(job_id, step_id, output_port)` and `relative_project_path` are unique.

`SqliteManagedFileRepository::add_generated_output` inserts the `managed_files` row and provenance row
inside one `BEGIN IMMEDIATE` transaction. If provenance insertion fails, the managed-file insertion is
rolled back rather than leaving an orphan record.

## 7. Idempotency and conflict handling

`OutputArtifactService` first looks up `(job, step, output port)`.

- If the existing file/provenance exactly matches the request, registration is idempotent and returns
  the existing artifact without another filesystem inspection, clock read, or ID generation.
- If identity matches but provenance differs, registration fails with `provenance_conflict`.
- If the relative output path belongs to another managed file, registration fails with
  `persistence_conflict`.
- Generated file identifiers use the existing bounded collision-retry policy.

Nothing is silently overwritten.

## 8. Lifecycle ingestion

`WorkerEventIngestionSession` accepts an artifact event only after `ready`, only while the persisted job
is `running`, and only when an `OutputArtifactService` is available. Job id, launch revision, and exact
sequence continuity are checked before registration.

If artifact registration throws, the event sequence is not advanced. Subsequent lifecycle events cannot
silently skip the failed artifact registration.

## 9. Demonstration coverage

The installed end-to-end fixture proves:

`ManagedFile input -> execution plan -> real worker -> real demo plugin -> output file -> artifact event -> Core reinspection -> SQLite generated ManagedFile/provenance`.

A second failure fixture makes the native demo plugin write a physical partial file and then exit with
code 3. The worker emits no artifact event and the project database contains zero generated artifacts
for that failed job.

## 10. Explicit boundaries

Iteration 015 does not yet provide:

- step-level atomic registration of multiple artifact events in one Core transaction;
- automatic cleanup/quarantine of physical partial files from failed jobs;
- generated-output checksums;
- artifact deletion/retention policy;
- artifact report/frontend endpoints;
- plugin sandboxing/signatures/resource limits;
- explicit filesystem durability flush (`fsync`/`FlushFileBuffers`);
- full TOCTOU elimination;
- native Windows/MSVC validation.

For a multi-output successful step, the worker guarantees that all files validate before it emits the
first artifact event. Core persistence is still event-by-event: a Core-side database/filesystem failure
mid-series can leave already registered sibling artifacts tied to a job that does not complete. A later
iteration should add a step-level artifact batch transaction before consumers treat multi-output sets
as atomic.
