# Project Database v1

## Scope

Iteration 005 adds the first project-local SQLite database at:

```text
<project-root>/.biocore/project.sqlite
```

The project database is distinct from the global `catalog.sqlite`. It owns project-specific metadata and the initial persistence schema for managed files, jobs, and project settings. This increment does not add repositories or use cases for those tables.

## Initialization boundary

`FilesystemProjectWorkspace` creates the project database while its workspace transaction is still uncommitted:

```text
workspace directories
  -> project.sqlite connection
  -> project migrations
  -> singleton project metadata
  -> ownership.json
  -> catalog repository save
  -> workspace commit
```

If database creation, migration, metadata insertion, or catalog persistence fails, the uncommitted workspace transaction removes only the exact OpenGenesis-BioCore artifacts it created. The database, WAL, and shared-memory sidecar paths are explicitly tracked for rollback.

## Migration runner

`ProjectMigrationRunner` is independent from `CatalogMigrationRunner` and maintains a separate `schema_migrations` history inside `project.sqlite`.

Version 1 creates:

- `project_metadata`
- `managed_files`
- `jobs`
- `settings`
- supporting indexes

Migration execution uses `BEGIN IMMEDIATE` and an exception-safe RAII rollback. Reapplying the current migration is idempotent. A database whose schema version is newer than the running OpenGenesis-BioCore build is rejected.

## Project metadata

`project_metadata` is a singleton table enforced by:

```sql
singleton INTEGER PRIMARY KEY CHECK(singleton = 1)
```

The initial row stores:

- project ID
- project name
- canonical root path
- creation timestamp
- update timestamp

Insertion uses prepared statements. A second initialization is rejected rather than silently replacing ownership metadata.

## Managed files foundation

The version 1 schema reserves metadata needed by future file-management use cases:

- display name
- storage mode
- original, managed, and project-relative paths
- file type
- size
- modification timestamp
- checksum algorithm and value
- creation and update timestamps

Accepted storage modes are:

- `managed_copy`
- `external_reference`
- `managed_move`
- `generated_output`
- `temporary`

Non-null project-relative paths are unique within a project database.

## Jobs foundation

The initial `jobs` table stores only the persistence foundation:

- job ID
- status
- priority
- progress
- active step ID
- creation and update timestamps

Its status constraint accepts every current `JobStatus` string. Queue scheduling, job repositories, state-transition persistence, and worker ownership are intentionally outside this increment.

## Settings foundation

The `settings` table stores a non-blank key, JSON text value, and update timestamp. JSON interpretation and settings services are deferred.

## Database isolation

The global catalog contains `catalog_projects` but no project-specific tables. A project database contains project-specific tables but no `catalog_projects` table. No cross-database transaction is claimed.

## SQLite policy

Every project database connection uses the existing SQLite connection policy:

- foreign keys enabled
- 5000 ms busy timeout
- WAL requested
- extended result codes enabled

The database is local-disk only. Network-filesystem operation is outside the supported model.

## Known boundaries

- Managed-file, job, and settings repositories are not implemented yet.
- Opening, validating, upgrading, or repairing an existing workspace is not implemented.
- Crash recovery for an orphaned workspace before catalog persistence is not implemented.
- Windows MSVC and Windows path case behavior remain unverified in the current environment.
- Standard filesystem path checks cannot remove every TOCTOU race without platform-specific handle APIs.
