# Project Research Context v1 — Iteration 079

## Baseline mapping

Frozen source: `accepted/iteration-078`, `0a19e5e8a56a939c5cbc53c5b5d94f17b1800689`.

| Existing capability | Authoritative implementation | 079 decision |
|---|---|---|
| Project identity and creation | `domain/project.hpp`, `application/project_service.hpp` | Reuse; no second Project type or creation service |
| Global project catalogue | `sqlite_project_repository.cpp`, catalog schema v1 | Unchanged; catalogue indexes identity/name/path, not scientific context |
| Project workspace ownership | `filesystem_project_workspace.cpp`, ownership.json | Unchanged; no new directory ownership mechanism |
| Project-local files/jobs/outputs | project SQLite tables, managed-file and job repositories | Remain owned by the selected project database |
| Workflow persistence/recovery | schema v9, `sqlite_workflow_state_store.cpp` | Unchanged; no scheduler, checkpoint or workflow rewrites |
| Sample registry/batch membership | Not implemented by 079 | Reserved for 080–084, never inferred from file names |
| Multi-sample scientific analysis | v0.3 matrix/association domains | Unchanged; sample registry must adapt to these later |

## Model and ownership

A project database contains one `project_metadata` row (`singleton=1`). Its existing
`project_id` is the canonical owner. Schema v10 adds research context to that same row:

- `research_description`: optional NUL-free text, at most 4096 UTF-8 bytes.
- `research_organism`: optional NUL-free text, at most 256 UTF-8 bytes; free text, not a verified taxonomy identifier.
- `research_revision`: nonnegative SQLite integer; new/migrated projects start at zero.
- `research_updated_at_utc`: copied from the existing update timestamp initially; subsequent values follow the existing Project nonblank/NUL-free timestamp contract (no new ISO parser).

The domain value and store never rename or move projects, modify the catalogue,
create jobs or run workflows. Reads require an explicit project ID; writes require
that ID and the expected revision. An unknown ID cannot implicitly create metadata.
The SQL identity trigger rejects in-place project ID changes. This is a data ownership
invariant, not multi-user authorization. Deletion/access-control policy is unchanged.

## Concurrency and failure semantics

`IProjectResearchMetadataStore::update` accepts a validated value with revision
`expected_revision + 1`. Negative/overflow/skipped revisions are invalid arguments.
One SQLite UPDATE with a project-ID and revision predicate performs compare-and-swap;
its RETURNING statement is drained to SQLITE_DONE before success is reported.
A stale or wrong-project update returns false. SQLite failures throw SqliteError;
callers must distinguish these from a revision conflict. No read-then-write race or
multi-database transaction is introduced. Callers must not share a connection with
uncoordinated operations inside an externally managed transaction.

SQL triggers enforce revision advancement for context or timestamp edits; the sole
exception initializes a newly inserted project's empty timestamp from its existing
Project timestamp. SQLite constraints enforce byte bounds, NUL exclusion and integer
revisions. The store preserves legacy project name/path/creation/update fields;
research context has its own update timestamp to avoid catalogue synchronization.

## Snapshot boundary

Returned values are copies: updating the project cannot mutate previously returned
values. This is the foundation for future run snapshots, NOT a persisted workflow
snapshot feature. Iteration 082 must capture the selected project/sample context in
its immutable plan; run/attempt association and export provenance arrive later.
Existing workflow JSON/checkpoint tables are not enriched or rewritten by 079.

## Migration and compatibility

The existing migration transaction now applies v10 after v9. Four additive columns,
three triggers and one migration record are committed together. A late failure must
roll back every v10 change and leave the v9 workflow readable. Fresh project insertion
through ProjectDatabaseInitializer also initializes research context. Migration is
idempotent; the global catalogue schema stays v1. New schema is forward-only: an old
v0.4 binary must not be used to write the migrated database; retain backups before
upgrading valuable data. No automatic downgrade is supplied.

The frozen v9 SQL fixture is extracted verbatim from the nine migration bodies at
baseline 078 (with the original schema_migrations table declaration). Tests build an
on-disk v9 project, close it, copy it, migrate the copy, reopen it and load its existing
workflow through the unchanged workflow store. The original copy remains v9.

## Exclusions and next contracts

No HTTP route, frontend screen, arbitrary metadata map, sample registry, batch runtime,
run snapshots, schema for speculative future relations or biological algorithm is
added here. Iteration 080 must key sample identities within the existing project;
081 must reference project-local managed files; 082–084 must distinguish batch/attempt
identity from existing workflow/job identity. The frozen 078 closure explicitly does
not claim an independent native multi-node dispatcher, so batch execution must map
only supported existing runtime capabilities and reject unsupported plans explicitly.
