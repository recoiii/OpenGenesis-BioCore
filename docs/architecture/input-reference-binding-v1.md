# Input & Reference Binding v1 — Iteration 081

Baseline: `accepted/iteration-080` / `36feaf30f18546d6c0eadf78bbe32b96af09bafc`.

## Scope

Iteration 081 adds a project-local relation from an existing sample to existing managed
input files and an optional existing managed reference file. It does not copy, move,
re-parent, rename or otherwise take ownership of those files. Managed-file ownership
continues to belong to the existing project database.

Supported sample input layouts are:

- `single_fastq`: one FASTQ managed file;
- `paired_fastq`: distinct R1 and R2 FASTQ managed files;
- `alignment`: one SAM or BAM managed file;
- `variants`: one VCF managed file.

A selected reference, when present, must be a FASTA managed file.

## Validation-before-binding

`SampleBindingService` validates the complete relation before persistence and repeats
the validation during commit. The service reuses the existing managed-file repository
and `IInputFileStorage::verify_managed_file` integrity contract, so missing files,
size/checksum changes, unsafe paths and unverifiable legacy/external files do not become
run-ready bindings.

Role validation is independent of filename extensions. R1/R2 roles require FASTQ
file types; the alignment role accepts SAM/BAM; the variants role accepts VCF; the
reference role accepts FASTA. A paired binding requires two distinct file identities.

A failed revalidation leaves any previously stored binding unchanged.

## Reference evidence

Reference compatibility is an explicit state; the presence of a selected FASTA never
means compatibility was proven.

- FASTQ contains no reference assembly declaration. FASTQ + FASTA therefore produces
  `not_declared_by_format` with a warning, not `verified`.
- SAM `@SQ` SN/LN declarations are compared with the selected FASTA.
- BAM binary reference dictionary names/lengths are compared with the selected FASTA.
- VCF `##contig` IDs and declared lengths are compared with the selected FASTA.
- SAM/VCF with no usable declaration remain `not_declared_by_format`.
- malformed/unreadable evidence is `evidence_unavailable` and blocks persistence.
- a declared missing contig or conflicting length is `mismatch` and blocks persistence.

Comparison requires every declared input contig to exist in the FASTA and every declared
length to agree. The FASTA may contain additional contigs because VCFs and filtered
alignment data can legitimately mention only a subset.

## Persistence

Schema v12 adds `project_sample_bindings`. The row uses foreign keys to the existing
`project_samples` and `managed_files` rows. The table stores only relationships:
it does not duplicate or mutate file path, checksum, storage mode or ownership metadata.

Deleting a sample cascades its binding. Files referenced by a binding use RESTRICT,
preventing silent orphaning while the relation exists.

## Forward boundary

Iteration 082 may snapshot a validated binding into a deterministic batch plan. It must
revalidate current file integrity/reference evidence before approving a plan and then
snapshot exact file/checksum/reference identities so later registry edits cannot rewrite
approved history.

No batch plan, scheduler submission, workflow run or retry behavior is implemented here.
