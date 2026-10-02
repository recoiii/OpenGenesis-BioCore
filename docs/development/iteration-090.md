# Iteration 090 — Cohort Registry & Group Definitions

Baseline: `accepted/iteration-089` / `9f85dd30cd774851beb101630e8577e17d4fb0de`.

## Scope

Iteration 090 implements only the persistent cohort registry promised by the accepted
089 contract. It adds explicit `case|control|unassigned` group labels, immutable
cohort revisions, ordered member snapshots, copied display metadata,
`biological_unit_id`, inclusion/exclusion state and reason, and optimistic
revision concurrency. Free-text `ProjectSample::group_label` remains metadata and
is never promoted automatically into a cohort phenotype.

Schema v16 adds `cohort_definitions`, `cohort_revisions`, and
`cohort_revision_members`. Cohort members reference the exact
`(project_id,sample_id)` registry key with `ON DELETE RESTRICT`, so a sample
cannot be silently deleted after it enters immutable cohort history. Revision
construction is transactional: a draft revision is inserted, all members are
validated and copied, the revision is sealed, and only then is the cohort's
`current_revision` advanced.

## Explicit non-scope

No VCF/artifact selection, reference proof, analysis preview/snapshot, matrix
dispatch, association execution, report, API route or UI is added here. Those remain
owned by iterations 091–097 exactly as defined in
`docs/architecture/cohort-workspace-v1.md`.

## Acceptance evidence

The 090 test gate covers group-token semantics; Unicode and leading-zero preservation;
duplicate and unknown/wrong-project sample rejection; immutable historical reads;
stale expected-revision conflicts; atomic failure rollback; sample deletion
restriction; and v15→v16 migration/rollback while preserving pre-existing rows.

The standard v0.6 validation workflow must pass all registered tests in GCC Debug,
GCC Release, Clang Debug and GCC ASan+UBSan. The review package is again four
Markdown parts plus the exact source ZIP and SHA-256 manifest. Gemini ACCEPT is
required before `accepted/iteration-090` is created.

Project owner/developer: Recep Çelik.
ChatGPT contribution: AI-assisted architecture, implementation, test design,
debugging, review-package preparation and release-process support.
