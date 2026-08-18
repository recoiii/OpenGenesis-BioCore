# Artifact Presentation and Integrity v1

## 1. Purpose

Iteration 018 adds a read-only presentation boundary for durable generated outputs and closes the
previous checksum gap. The iteration does not add HTTP endpoints or a browser UI. It establishes the
Application, Infrastructure, and Presentation contracts that a future Drogon adapter can consume.

## 2. Registration-time SHA-256

Generated output registration remains zero-trust with respect to the worker. During Core-side output
inspection, `FilesystemOutputArtifactInspector`:

1. resolves the expected flat project output path,
2. rejects symlinks and non-regular files,
3. records size and modification time,
4. streams the file through the local C++20 SHA-256 implementation,
5. repeats path, type, size, and modification-time checks after hashing,
6. returns lowercase SHA-256 metadata to `OutputArtifactService`.

`OutputArtifactService` independently requires algorithm `sha256` and exactly 64 lowercase hexadecimal
characters before creating the `ManagedFile`. The checksum therefore becomes part of the same durable
managed-file/provenance batch that already has step-level transaction semantics.

The hash is synchronous. Large biological outputs may make registration CPU/I/O intensive; background
hash progress and cancellation are outside this iteration.

## 3. Project schema v5

Schema v5 adds SQLite INSERT/UPDATE triggers for new `StorageMode::generated_output` rows. New generated
outputs cannot be persisted without algorithm `sha256` and a lowercase 64-hex value, even if a caller
bypasses `OutputArtifactService` and talks directly to the repository adapter.

The migration deliberately does not fabricate checksums for artifacts created by schema versions <=4.
Legacy generated outputs may therefore remain checksum-null. They are still listable and reportable,
but download verification returns `checksum_unavailable` until a future explicit backfill policy exists.

## 4. Artifact read model

`ArtifactPresentationService` is Application-owned and depends only on:

- `IManagedFileRepository`,
- `IJobRepository`,
- `IArtifactContentAccess`,
- `IUtcClock`.

It provides:

- generated-artifact listing by Job,
- generated-artifact listing by step,
- artifact detail lookup,
- integrity-verified download preparation,
- a deterministic pipeline execution report read model.

Missing Jobs are distinct from existing Jobs with no artifacts. Artifact lists are sorted deterministically
by `step_id`, `output_port`, then `managed_file_id`, independent of adapter row order.

## 5. Download verification boundary

`FilesystemArtifactContentAccess` treats both persisted metadata and the physical filesystem as
untrusted input. Before returning a download descriptor it verifies:

- generated-output storage mode,
- persisted managed path and project-relative path agreement,
- the exact flat `outputs/<filename>` namespace,
- provenance path agreement,
- non-symlink regular-file status,
- canonical containment and exact expected path,
- persisted size,
- persisted SHA-256 availability and format,
- a freshly computed SHA-256 value,
- path/type/size/mtime stability after hashing.

A same-size content substitution therefore fails with `checksum_mismatch` rather than passing the size
check. The Application service maps physical verification results to typed presentation errors.

`ArtifactDownloadDescriptor::content_path` is an internal Application-to-adapter descriptor. A future
HTTP adapter must stream from it internally and must not serialize or expose the absolute local path to
clients.

## 6. Report read model and rendering

`PipelineExecutionReport` contains Job status, priority, progress, pipeline identity, timestamps,
revision, generation timestamp, and deterministic artifact metadata including persisted checksums.

The Presentation target provides deterministic JSON and static HTML renderers. JSON escapes quotes,
backslashes, control characters, and standard escaped characters. HTML escapes `&`, `<`, `>`, `"`, and
`'`. Neither report format includes absolute content paths.

Durable execution logs do not exist yet, so this report foundation intentionally does not claim to
render execution logs. Report publication to a file/store is also deferred.

## 7. Dependency direction

```text
presentation report renderer
        -> application read DTOs
        -> domain enums

infrastructure filesystem verifier / SHA-256 / SQLite v5
        -> application ports
        -> domain

application ArtifactPresentationService
        -> domain
        -> application-owned ports only
```

No filesystem, SQLite, JSON/HTML rendering, or OS API is introduced into Domain or Application.

## 8. Security properties

- no network access or telemetry is added,
- no shell command construction is added,
- no absolute patient/project paths are rendered into reports,
- same-size file tampering is detected before download preparation,
- legacy checksum-less artifacts fail closed for verified download,
- HTML output escapes text fields before insertion,
- SQLite rejects new checksum-less or malformed generated outputs.

## 9. Explicit boundaries

Iteration 018 does not provide:

- Drogon HTTP endpoints, WebSocket routes, or browser UI,
- durable execution logs in reports,
- report file publication, signing, or retention,
- checksum backfill for legacy schema <=4 outputs,
- asynchronous/progress-aware hashing for very large outputs,
- cryptographic signatures or publisher trust,
- handle-based immutable file serving or complete TOCTOU elimination,
- explicit filesystem durability flush (`fsync`/`FlushFileBuffers`),
- native Windows/MSVC runtime validation.

The verifier narrows TOCTOU by rechecking metadata after hashing, but a future server must avoid a long
gap or unsafe path reopen between `prepare_download` and actual streaming.
