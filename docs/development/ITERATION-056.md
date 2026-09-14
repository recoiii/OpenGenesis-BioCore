# OpenGenesis-BioCore v0.3.0-dev — Iteration 056

## Title
VCF Ingestion, Indexing & Normalization

## Accepted baseline

- Iteration 055 accepted ref: `accepted/iteration-055`
- Exact accepted baseline commit: `1e16121a051a679c275e3263a2ba176a384fd633`

## Objective

Establish the single canonical ingestion boundary for external and future BioCore-produced VCF records. The iteration parses VCF text into the shared Iteration 055 domain model, validates REF against an explicit FASTA reference, normalizes alleles deterministically, and provides a reusable coordinate index for non-linear regional lookup.

## Scope

- Strict VCFv4.2-v4.5 text ingestion for the subset represented by the shared domain model.
- 1-based VCF POS to 0-based half-open internal coordinate conversion through the Iteration 055 adapters.
- Multi-allelic ALT parsing without silent allele loss or ALT reordering.
- Typed INFO ingestion for declared Integer, Float, String, Character and Flag fields, preserving scalar missing and element-level missing values.
- GT/AD/DP/GQ/PL sample FORMAT ingestion with ploidy and per-separator phasing preservation.
- Assembly-aware FASTA loading and contig alias resolution for primary GRCh37/GRCh38 contigs.
- Fail-closed REF validation against the loaded FASTA.
- Canonical common-prefix/common-suffix trimming while retaining legal anchor bases.
- FASTA-backed left alignment for simple biallelic insertions and deletions.
- Deterministic, idempotent canonical normalization.
- In-memory coordinate index storing only interval metadata plus stable external record ordinals.
- Prefix-max assisted overlap lookup without full-record duplication.
- Regression fixtures and a 1M-entry coordinate-index performance evidence job.

## Non-goals

- BCF binary parsing.
- Tabix/CSI binary compatibility or a persistent on-disk index format.
- Structural-variant normalization, breakend grammar, or symbolic-allele interpretation beyond safe UNKNOWN ingestion.
- Variant calling/local re-alignment (Iteration 057).
- Probabilistic genotyping (Iteration 058).
- Replacing the legacy v0.2 VCF QC plugin parser in this iteration.

## Normative invariants

1. No VCF record enters the canonical model with POS=0, empty REF, empty ALT, or unresolved CHROM.
2. REF mismatch against FASTA is a hard ingestion failure.
3. Multi-allelic ALT order and genotype allele indices are preserved.
4. Missing values remain distinguishable from zero, empty vectors, and absent fields.
5. Reserved AF/AC/AN/DP/MQ and GT/AD/DP/GQ/PL fields are type/cardinality checked and negative/range-invalid values are rejected where semantically invalid.
6. GT, when present, is the first FORMAT field and per-separator phasing survives ingestion.
7. Normalization is deterministic and idempotent.
8. Normalization must not change the represented FASTA haplotype.
9. Simple biallelic indels are left-aligned as far as reference context permits.
10. Every normalized record remains valid under Iteration 055 `VariantRecord` invariants.
11. Coordinate index construction is deterministic and queries are contig/range bounded.
12. The coordinate index owns interval/index metadata only; it never owns duplicate `VariantRecord` objects.
13. Unsupported constructs fail explicitly or remain safely UNKNOWN; they are never silently reinterpreted.
14. Iteration 057 DoD requires BioCore caller output to pass this exact canonical boundary without special-case repair.

## Definition of Done

- VCF ingestion tests cover multiallelic records, typed INFO, `1,.,3` missing preservation, phased GT, AD Number=R and PL Number=G.
- Malformed POS, undeclared INFO and FASTA REF mismatch fail closed.
- Normalization fixtures prove homopolymer left-alignment, multi-allelic ALT-order preservation and idempotence.
- Coordinate index fixtures prove exact overlap semantics and deterministic ordering.
- A 1M-entry index benchmark reports build/query evidence without correctness loss.
- All Iteration 055 tests remain green.
- GCC Debug, GCC Release, Clang Debug and GCC ASan+UBSan pass the exact active CTest inventory.
- Gemini package contains exactly four bounded Markdown parts with every Iteration-055-relative changed file embedded exactly once and without truncation.

## Acceptance rule

Iteration 056 may be frozen only after independent Gemini review returns `VERDICT: ACCEPT`. A rejection keeps the same iteration number and requires revision plus a full CI rerun.
