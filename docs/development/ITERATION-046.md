# OpenGenesis-BioCore v0.2.0-dev — Iteration 046

## Title

Project Database Compatibility & Integrity Guard

## Goal

Fail fast before the local runtime starts when an existing project database is internally inconsistent, has an invalid or unsupported migration ledger, contains foreign-key violations, or no longer contains the critical schema objects required by project schema v6.

Iteration 046 preserves schema v6. It adds validation around the existing migration path; it does not add schema v7 and does not attempt automatic repair.

## Intended changes

- add `ProjectDatabaseGuard` in the SQLite infrastructure layer;
- run SQLite `PRAGMA quick_check` and `PRAGMA foreign_key_check` before migration and after migration;
- require an existing `schema_migrations` ledger to be an exact contiguous prefix of the six known migration records before any migration mutates the project database;
- reject future, gapped or renamed migration histories;
- after migration, require the complete schema-v6 migration ledger and critical tables/triggers;
- wire the guard into `biocore --serve` before and after `ProjectMigrationRunner::apply_pending()`;
- add a dedicated CTest covering clean databases, ledger gaps/name tampering, future schemas, missing required objects and foreign-key violations;
- raise the active Linux CI test floor from 67 to 68 while retaining the v0.1 67-test baseline as a permanent minimum.

## Explicit non-goals

Iteration 046 must not:

- change `latest_project_schema_version` from 6;
- add or edit a database migration;
- rewrite, repair or delete user data when validation fails;
- change catalog schema behavior;
- change Worker Protocol v2;
- change plugin/pipeline identifiers or versions;
- change biological calculations;
- change the loopback-only network/security boundary.

## Acceptance criteria

1. A clean pre-schema database is accepted before migration and validates as current after the existing migrations run.
2. A valid v0.1/v0.2 schema-v6 database passes the guard without mutation.
3. Gapped, renamed, non-contiguous or future migration ledgers are rejected before migration is allowed to mutate the database.
4. `PRAGMA quick_check` must return exactly `ok`.
5. `PRAGMA foreign_key_check` must return no violations.
6. Current schema validation requires the schema-v6 migration ledger and critical project tables/triggers.
7. The local server runs validation before and after `ProjectMigrationRunner::apply_pending()` and never starts runtime services after a guard failure.
8. The new guard has dedicated regression coverage and the active CTest floor is at least 68.
9. GCC Debug, GCC Release, Clang Debug and GCC ASan+UBSan all pass the complete test suite.
10. `0.2.0-dev`, schema v6, Worker Protocol v2, compatibility identifiers and biological behavior remain unchanged.
11. Independent Gemini review returns `ACCEPT` before Iteration 046 is frozen.

## Freeze rule

Iteration 046 remains open until the exact final candidate passes the Linux validation matrix and independent Gemini review. A blocking finding keeps the iteration open and requires a revised candidate.
