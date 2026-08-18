# Data Persistence v1

## Status

Iteration 002 accepted and frozen.

## Scope

This increment proves that OpenGenesis-BioCore can persist project catalog metadata while preserving the accepted Clean Architecture direction.

```text
Domain Project
      ^
      |
Application IProjectRepository
      ^
      |
Infrastructure SqliteProjectRepository
      |
SQLite catalog database
```

## Components

### Domain

`biocore::domain::Project` owns basic invariants:

- non-blank ID, name, root path, creation timestamp, and update timestamp
- no embedded NUL characters
- ID length at most 128 bytes
- name length at most 200 bytes

Filesystem canonicalization and timestamp parsing are intentionally outside this increment.

### Application

`IProjectRepository` defines four persistence operations:

- save
- find by ID
- list
- remove

Saving an existing project updates mutable fields but preserves the original creation timestamp.

### Infrastructure

`SqliteConnection` owns one SQLite connection and configures:

- foreign keys enabled
- 5000 ms busy timeout
- WAL journal mode request
- extended result codes
- UTF-8 database paths, including Windows paths converted through `std::filesystem::path::u8string()`

`CatalogMigrationRunner` maintains `schema_migrations`, applies schema version 1 transactionally, is idempotent, and rejects databases created by a newer unsupported schema.

`SqliteProjectRepository` uses prepared statements for all user-provided values. The catalog schema enforces unique project IDs and exact-text unique root paths.

## Ownership and concurrency

The accepted architecture gives the main OpenGenesis-BioCore process sole write ownership of catalog and project databases. Worker processes do not access these databases directly. Iteration 002 therefore does not introduce multi-process migration coordination.

## Known boundaries

- Project creation now canonicalizes existing directories in Iteration 003. Windows host validation and nuanced case-sensitivity behavior remain pending.
- Project timestamps are stored as canonical UTC strings supplied by higher-level application services. A dedicated timestamp value object is deferred.
- SQLite is discovered as a system/developer dependency; distributable Windows dependency packaging is deferred.
- `IJobRepository` and the project-local database are outside this iteration.
