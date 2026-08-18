# Project Creation v1

## Status

Iteration 003 candidate for independent review.

## Scope

This increment introduces the first Application use-case service for project registration while preserving the accepted dependency direction.

```text
ProjectService
  -> IProjectRepository
  -> IIdGenerator
  -> IUtcClock
  -> IPathCanonicalizer

Infrastructure adapters
  -> SqliteProjectRepository
  -> UuidV4Generator
  -> SystemClock
  -> FilesystemPathCanonicalizer
```

## Application orchestration

`ProjectService::create` performs the following sequence:

1. canonicalize the supplied existing project-root directory,
2. reject a root already registered under the same canonical path,
3. generate an identifier and reject collisions, retrying at most eight times,
4. obtain one UTC timestamp for both creation and initial update time,
5. construct the validated Domain `Project`,
6. persist it through `IProjectRepository`.

The Application layer owns the orchestration but does not use SQLite, `std::filesystem`, random engines, or operating-system clock APIs.

## Ports

- `IProjectRepository` now supports exact canonical-root lookup.
- `IIdGenerator` supplies opaque identifiers.
- `IUtcClock` supplies canonical UTC timestamps.
- `IPathCanonicalizer` converts a user-supplied path into the canonical identity of an existing directory.

## Infrastructure adapters

### `UuidV4Generator`

Generates lowercase RFC 4122-style version 4 UUID strings and sets both the version and variant bits explicitly.

### `SystemClock`

Implements `IUtcClock` and emits second-precision UTC timestamps in `YYYY-MM-DDTHH:MM:SSZ` form.

### `FilesystemPathCanonicalizer`

- rejects blank paths and embedded NUL characters,
- requires the target to exist and be a directory,
- resolves `.` and `..`,
- resolves symbolic links where the platform supports them,
- preserves UTF-8 paths when crossing the Application boundary.

This iteration registers an **existing directory**. Creation of the directory structure and ownership markers is intentionally deferred to the future project-workspace increment.

## Duplicate prevention

The service checks the canonical root before creating a project. The catalog database retains its unique `root_path` constraint as the authoritative persistence safeguard.

The accepted architecture assigns catalog writes to the main OpenGenesis-BioCore process. Under that single-writer model, the identifier collision check followed by repository save is serialized by the future composition root. Cross-process project creation is not supported.

## Known boundaries

- Windows MSVC execution has not yet been performed.
- Standard-library canonicalization is compiled for Windows but case-preserving/case-folding behavior has not been validated on a Windows host.
- Per-directory case-sensitive behavior on Windows is not modeled separately.
- UUID generation is intended for uniqueness, not as an authentication secret.
- Timestamp parsing and a dedicated timestamp value object remain deferred.
- Filesystem directory creation, `.biocore` ownership metadata, and rollback of partially created workspaces are outside this iteration.
