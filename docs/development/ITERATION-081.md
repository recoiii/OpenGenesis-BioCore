# Iteration 081 — Input & Reference Binding

Status: CANDIDATE, awaiting independent Gemini review. Not accepted or frozen.
Baseline: `accepted/iteration-080` / `36feaf30f18546d6c0eadf78bbe32b96af09bafc`.
Development branch: `v0.5.0-dev`.
Development identity: `0.5.0-dev`.

## Objective

Bind existing project samples to existing managed inputs and optional references without
changing managed-file ownership, while detecting missing/changed files, invalid file
roles and declared reference incompatibility before a binding becomes run-ready.

## Deliverables

- `ProjectSampleBinding` domain model for single FASTQ, paired FASTQ, SAM/BAM and VCF.
- Preview/commit validation using existing managed-file and integrity contracts.
- Explicit reference-evidence states; FASTQ is never reported as reference-verified.
- Filesystem SAM/BAM/VCF-to-FASTA reference dictionary inspector.
- Project-local SQLite binding persistence using foreign keys only.
- Transactional v11→v12 migration and rollback evidence.
- Frozen v11 SQL fixture.
- Dedicated domain, roles, integrity, reference, persistence, inspector, migration and
  rollback test groups.
- Retained Iteration 079/080 and v0.4 regression coverage.

## Acceptance gates

1. Missing or checksum/size-changed managed inputs block commit.
2. Paired R1/R2 must be distinct FASTQ files; role/type mismatches block commit.
3. SAM/BAM/VCF reference declarations that disagree with the selected FASTA block commit.
4. FASTQ or declaration-free formats remain explicitly unverified rather than being
   reported as reference-compatible.
5. Binding persistence does not change managed-file count, path, storage mode or checksum.
6. v11→v12 migration preserves samples, research metadata, managed files and prior workflows.
7. Injected late v12 failure rolls back the binding table and migration record.
8. Iteration 082 batch planning/execution remains out of scope.
9. Independent Gemini ACCEPT on the exact candidate is required before freeze.

Project owner/developer: Recep Çelik. AI-assisted implementation, architecture,
validation and review-package preparation: ChatGPT. Independent review is pending.
