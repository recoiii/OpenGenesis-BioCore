# Iteration 079 — Scope & Project Model

Status: CANDIDATE, awaiting independent Gemini review. Not accepted or frozen.
Baseline: `accepted/iteration-078` / `0a19e5e8a56a939c5cbc53c5b5d94f17b1800689`.
Development branch: `v0.5.0-dev`. Development identity: `0.5.0-dev`.

## Objective

Reuse existing project/catalogue/workspace ownership and introduce bounded,
revision-controlled research context on the existing singleton project metadata.
See `docs/architecture/project-research-context-v1.md` for scope mapping and contracts.

## Deliverables

- Validated ProjectResearchMetadata domain value and application store interface.
- Project-local SQLite store with atomic compare-and-swap and explicit project ID.
- Transactional v9→v10 migration, defaults for fresh/legacy projects, schema guard.
- Legacy v1/v3/v6 job-test fixtures now include the original project_metadata table
  that real project databases already contain; production migration remains strict.
- Frozen v9 SQL fixture and five new CTest groups: domain, persistence, conflict,
  migration, rollback; retained v0.3 upgrade and workflow regression tests.
- Exact candidate source archive; four Markdown Gemini review parts embedding each
  changed text file exactly once; SHA256SUMS file for source and review evidence.

## Acceptance gates

1. Source compiles with warnings-as-errors; new behavior tests and retained suite pass.
2. Project identity/name/path and legacy jobs/files/workflows survive migration.
3. Wrong-project/stale updates do not write; metadata copies remain unchanged.
4. Late migration failure leaves schema v9 intact and allows subsequent successful retry.
5. Full evidence must state actual tested configurations and untested environments.
6. Independent Gemini ACCEPT must identify this exact candidate before freeze.
7. No iteration 080 implementation before 079 ACCEPT. Existing accepted refs and main remain untouched.

Every v0.5 iteration requires Gemini ACCEPT; final 088 closure requires both Gemini
and Claude ACCEPT on the same final source identity. A prepared review package is
not a reviewer verdict. Review findings require a new candidate and new hashes.

Project owner/developer: Recep Çelik. AI-assisted implementation, design, validation
and review-package preparation: ChatGPT. Independent reviews are pending.
