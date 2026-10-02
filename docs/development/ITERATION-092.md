# Iteration 092 — Cohort Matrix Construction & Dispatch Boundary

Opened 2026-10-02. Candidate only; independent Gemini review pending.

Frozen baseline:
- Ref: `accepted/iteration-091`
- Commit: `d6fada4d5a84b8f4de62f02b01bd1b5dd157bf33`
- Tree: `1756bdd23295006bdda5d1f0e177a8407649848e`

## Scope implemented

Iteration 092 consumes the accepted 091 explicit selection contract and constructs the
canonical cohort matrix without changing the retained scientific engines.

The matrix build path:
1. Re-runs the 091 selection preview on the exact cohort revision and explicit
   artifact/run/attempt/VCF-column mappings.
2. Reloads the selected reference FASTA from managed storage and verifies size/SHA-256
   before and after loading.
3. Derives a deterministic target contig table from the reverified reference and the
   pinned explicit alias map.
4. Re-verifies every selected generated VCF.
5. Parses each physical VCF artifact once even when multiple selected samples use
   different columns from the same multi-sample VCF.
6. Projects only the explicitly selected column into a one-sample
   `VcfIngestionResult` and renames that sample to the exact project sample ID.
7. Calls the unchanged `build_multi_sample_matrix()` engine with the v0.6 integration
   resource envelope.

Unselected VCF columns do not enter the matrix. Sample IDs are not normalized,
trimmed, case-folded or numerically coerced.

## Scientific eligibility gate

Normalization remains owned by the existing `ingest_vcf()` path and the exact pinned
FASTA. After normalization, cohort v1 accepts:
- literal SNVs,
- insertions with normalized inserted length 1..50,
- deletions with normalized deleted length 1..50.

Cohort v1 rejects:
- symbolic/SV/breakend alleles,
- MNVs,
- complex/delins substitutions,
- unknown unsupported allele types,
- normalized indels >50 bases.

Diploid ploidy validation remains the earlier 091 admission gate and is re-entered
because 092 re-runs the full 091 preview before matrix work.

## Sparse and ordering semantics

The retained sparse engine is reused unchanged. If one selected sample has no VCF
record at a locus present in another selected source, no observation is created for
the absent sample. Absence is therefore not converted to hom-ref or no-call.

Matrix sample ordering follows the retained exact UTF-8 sample-ID ordering.
Variant-allele ordering follows the deterministic target contig table, locus, REF and
ALT representation, not request order or VCF record order.

## Resource envelope

092 enforces:
- <=100 selected/included samples,
- <=100 physical selected VCF artifacts,
- <=10,000 normalized variant alleles,
- <=1,000,000 sample/allele observations.

The per-VCF, combined VCF-text and reference FASTA byte limits remain enforced by the
accepted 091 selection layer before the matrix engine is called.

## Dispatch boundary

092 emits a deterministic matrix-stage descriptor:
- stage: `matrix`,
- native module identity: `org.biocore.cohort.matrix`,
- project/cohort/revision identity,
- normalization-contract version,
- pinned reference file/hash,
- exact selected project-sample/artifact/hash/VCF-column mappings,
- matrix resource ceilings.

This descriptor is the fixed boundary consumed by later execution hardening. 092 does
not add a second scheduler, does not execute scientific work on an HTTP thread and
does not persist an attempt reservation. Durable JobScheduler/worker handoff,
idempotent submission, retry/cancel and crash reconciliation remain Iteration 095.

## Explicit non-scope

092 does not:
- persist the immutable analysis snapshot,
- apply QC inclusion/exclusion approval,
- calculate case/control association,
- persist cohort execution attempts,
- change JobScheduler recovery semantics,
- add HTTP/browser UI,
- change project schema v16.

QC approval and immutable analysis snapshot ownership remain 093. Association remains
094. Execution/recovery hardening remains 095.

## Verification gate

Nine dedicated CTest modes cover:
1. shared multi-sample VCF parsed once with exact project-ID column projection,
2. sparse absence remaining absent,
3. deterministic sample and variant ordering,
4. MNV/complex/oversized-indel cohort-v1 rejection,
5. exact resource boundary validation,
6. explicit custom reference alias projection,
7. reference-byte drift rejection before matrix construction,
8. real managed-filesystem FASTA loading and tamper rejection,\n9. deterministic matrix-stage dispatch descriptor.

Expected full CTest inventory: 317 tests (308 inherited + 9 new).

The standard four Linux lanes and retained cohort benchmark must pass on the exact
candidate commit. The Gemini package remains exactly four Markdown files plus the
exact source ZIP and SHA-256 manifest. Do not create `accepted/iteration-092` and do
not begin 093 before independent Gemini ACCEPT for the exact candidate identity.

Project owner/developer: **Recep Çelik**.

**ChatGPT contribution:** AI-assisted architecture, implementation, validation logic,
test design, debugging, documentation, source-identity verification and independent
review-package preparation.
