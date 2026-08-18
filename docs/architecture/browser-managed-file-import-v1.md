# Browser Managed File Import Architecture v1

## Status

Iteration 028 candidate.

## Purpose

Iteration 028 closes the first user-facing input gap: a browser can select a local file, stream it to
OpenGenesis-BioCore over the already authenticated loopback API, persist it as a normal project `ManagedFile`, and
bind the resulting identifier into the existing FASTA QC submission flow.

No second file repository, upload database, workflow engine, or browser filesystem-path trust model is
introduced.

## Browser constraint

Browser file inputs intentionally do not expose the user's real local filesystem path. OpenGenesis-BioCore therefore
does not attempt to reuse the source-path copy endpoint from browser JavaScript. File bytes are uploaded
in bounded chunks and staged under a OpenGenesis-BioCore-owned runtime directory.

## Application boundary

`ManagedFileService` remains the single managed-input use-case service. It now owns an in-memory bounded
browser-upload session map and delegates physical staging to the existing `IInputFileStorage` port.

Policy bounds:

- maximum active sessions: 8;
- maximum chunk: 1 MiB;
- maximum declared file size: 8 GiB;
- chunks must arrive at the exact next byte offset;
- completion requires received bytes to equal the declared total;
- final staged file size is rechecked before persistence.

The staging session is not durable state. Restarting Core discards stale browser-upload staging rather
than pretending that an incomplete upload can resume safely.

## Filesystem staging and commit

Infrastructure owns:

```text
<project>/.biocore/runtime/browser-uploads/<upload-id>/<display-name>
```

Only safe direct-child identifiers and one-file staging directories are accepted. Symlinks and
unexpected directory shapes are rejected or skipped rather than followed.

On completion the staged file is renamed, on the same project filesystem, to:

```text
<project>/inputs/<managed-file-id>/<display-name>
```

The returned `IInputFileImportTransaction` preserves the existing registration ordering:

```text
staging file
  -> rename to managed input destination
  -> construct ManagedFile
  -> SQLite repository add
  -> commit filesystem transaction
```

If persistence fails, destruction of the uncommitted transaction attempts to rename the file back to
its staging location and removes only the newly created destination directory. This avoids a second
multi-gigabyte copy while preserving rollback semantics.

## REST surface

Authenticated endpoints:

- `GET /api/v1/files`
- `GET /api/v1/files/{managedFileId}`
- `POST /api/v1/files/uploads`
- `POST /api/v1/files/uploads/{uploadId}/chunks`
- `POST /api/v1/files/uploads/{uploadId}/complete`
- `POST /api/v1/files/uploads/{uploadId}/cancel`

Upload start uses strict JSON with `displayName`, `fileType`, and non-negative `sizeBytes`.

Chunk requests require:

- `Content-Type: application/octet-stream`;
- `X-BioCore-Upload-Offset`;
- body size 1..1,048,576 bytes.

Chunk bodies are arbitrary binary data and are therefore not subjected to JSON/UTF-8 body validation.
Request metadata remains UTF-8/NUL validated and bounded.

Cookie-authenticated POST requests retain the Iteration 024 exact-local-Origin requirement. Bearer-token
compatibility remains unchanged.

## Response privacy

Managed-file API responses expose only:

- ID;
- display name;
- storage mode;
- file type;
- size;
- creation timestamp.

Original paths, staging paths, managed absolute paths, and project-relative paths are not serialized to
the browser.

## Frontend flow

The browser dashboard now provides:

```text
Select local FASTA
  -> start upload
  -> stream 1 MiB chunks
  -> complete upload
  -> receive managedFileId
  -> select managed input
  -> populate org.biocore.fastaqc.summary@0.1.0
  -> populate stats/source managed-file binding
  -> submit through existing Job API
```

The generic JSON binding editor remains available. The FASTA helper only prepares the already accepted
binding schema; it does not bypass server-side binding validation.

The frontend continues to avoid `innerHTML`, browser storage, token-in-URL patterns, and
`document.cookie` access.

## Explicit boundaries

Iteration 028 does not provide:

- resumable upload sessions across Core restart;
- parallel/out-of-order chunk upload;
- upload hashing before managed-file persistence;
- arbitrary 8+ GiB browser uploads;
- browser directory import;
- automatic biological file-type detection;
- native Windows/MSVC execution proof;
- public remote upload or non-loopback serving.

Generated-output SHA-256 policy remains unchanged. Input-file checksum/backfill policy may be added in a
future release-hardening iteration.
