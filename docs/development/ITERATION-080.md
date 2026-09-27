# Iteration 080 — Sample Registry

Status: CANDIDATE, awaiting independent Gemini review. Not accepted or frozen.
Baseline: `accepted/iteration-079` / `23a51bf32388965c558f9d909efc357398975579`.
Development branch: `v0.5.0-dev`. Development identity: `0.5.0-dev`.

## Objective

Add a project-local sample registry with deterministic CSV/TSV preview, atomic import
and export while preserving opaque sample identifiers and the Iteration 079 ownership
model.

## Deliverables

- Validated `ProjectSample` domain value.
- Application-level preview/commit/export service with row-numbered diagnostics.
- Project-local SQLite sample store with all-or-none batch insert.
- Transactional v10→v11 migration and schema guard.
- Frozen v10 SQL fixture for migration/rollback evidence.
- Dedicated domain, preview, persistence, import, round-trip, migration and rollback tests.
- Retained full v0.5 regression suite.

## Acceptance gates

1. Leading-zero IDs, Unicode and empty optional metadata round-trip unchanged.
2. Duplicate/invalid rows are identified with their physical input line.
3. Invalid previews cannot write; stale or conflicting commits leave zero partial rows.
4. Migration from a copied v10 project preserves project/research metadata and reaches v11.
5. Injected late v11 migration failure rolls back the new table and migration record.
6. Existing workflow/project behavior and Iteration 079 contracts remain green.
7. Independent Gemini ACCEPT on the exact candidate is required before freeze.
8. No Iteration 081 implementation before 080 ACCEPT.

Project owner/developer: Recep Çelik. AI-assisted implementation, architecture,
validation and review-package preparation: ChatGPT. Independent review is pending.
