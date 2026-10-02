# Iteration 090 — Cohort Registry & Group Definitions

Opened 2026-10-02. Candidate only; independent Gemini review pending. The frozen
starting point is `accepted/iteration-089`, commit
`9f85dd30cd774851beb101630e8577e17d4fb0de`, source tree
`18b5d4c5dab896b2b322ff29e105fa69296d9c53`. Iteration 089 is not modified.

## Scope implemented

Iteration 090 adds the project-bound cohort registry defined by the accepted 089
contract. A cohort has an immutable identity and a monotonic current revision. Every
revision stores an ordered, immutable membership snapshot with exact project sample
ID, copied display/group metadata, biological-unit ID, explicit
`case|control|unassigned` group, inclusion/exclusion disposition and an explicit
reason for exclusions. Missing group therefore remains `unassigned`; it is never
coerced to control.

The application service validates cohort names, IDs, member IDs, duplicate samples,
exclusion semantics and optimistic expected revision. The SQLite store rechecks
project/sample ownership inside `BEGIN IMMEDIATE`, writes a complete unsealed
revision, seals it, then atomically advances the cohort's current revision. A stale
revision, missing/wrong-project sample, duplicate sample, constraint failure or
injected SQLite failure leaves no partial new revision.

## Persistence delta

Project schema advances additively from v15 to v16. New tables are
`cohort_definitions`, `cohort_revisions` and `cohort_revision_members`; historical
migrations are unchanged. Foreign keys bind definitions to the project, members to
both their revision and exact project sample, and parent revision to its preceding
revision. Sealed revision/member mutation and deletion are blocked by triggers.
Referenced samples use `ON DELETE RESTRICT`, so a mutable sample registry operation
cannot silently erase immutable cohort history.

`ProjectDatabaseGuard` now recognizes migration 16 and the required cohort tables and
immutability triggers. Existing v0.3/v0.4/v0.5 migration compatibility tests are
updated only where their asserted *current* schema version changed; their historical
v15 recovery semantics remain intact.

## Verification and acceptance criteria

Eight dedicated tests are added: model tokens/defaults, create/snapshot round-trip,
validation failures, revision history, transactional failure injection, immutability,
v15→v16 migration, and v16 migration rollback/retry. They explicitly cover leading
zero and Unicode sample IDs, conflicting duplicate group assignments, missing sample,
wrong project, `unassigned` default, explicit exclusion reason, metadata changes after
a historical snapshot, stale optimistic revision, rollback on partial member insert,
and deletion/mutation resistance.

The full registered CTest inventory is expected to be 300 tests (292 inherited + 8
new). The v0.6 workflow requires all 300 to pass independently in Linux GCC Debug,
GCC Release, Clang Debug and GCC ASan+UBSan, with `0.6.0-dev` identity. The unchanged
089 cohort-engine benchmark is rerun as regression/resource evidence; it is not a
claim that 090 adds or changes matrix/association performance.

## Explicit boundaries

090 does **not** choose run/attempt/artifact/VCF columns or reference evidence (091),
does not construct a cohort matrix (092), does not approve QC membership (093), does
not call the case/control association engine (094), and does not implement cohort
execution/recovery/results/UI. The application operations introduced here are wired
into the integrated HTTP/browser workspace in the later integrated-workspace scope;
no new direct scientific execution path is added.

No patient data are added. Project owner/developer: **Recep Çelik**. **ChatGPT
contribution:** AI-assisted architecture-to-code translation, C++/SQLite
implementation, migration and regression-test design, debugging, CI/review packaging
and source-identity verification. Independent Gemini review remains the acceptance
gate.

## Gate

This is a candidate, not an accepted iteration. Generate exactly four Markdown review
parts plus the exact candidate source ZIP and SHA-256 manifest from the same commit.
Do not create or move `accepted/iteration-090`, and do not start 091, unless Gemini
returns ACCEPT for that exact candidate identity. Any source change after review
requires refreshed evidence and a new review package.
