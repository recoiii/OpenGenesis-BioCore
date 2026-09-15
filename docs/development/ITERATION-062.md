# Iteration 062 — Annotation 2.0 Engine

## Baseline

- Accepted baseline: `accepted/iteration-061`
- Exact baseline SHA: `a13fecfd765f1c80cd3070e333765306ae91bd2e`
- Development line: `v0.3.0-dev`

## Objective

Build an assembly-aware, provenance-preserving Annotation 2.0 domain engine on top of the generic reference-database framework introduced by Iteration 061.

## Contract

Iteration 062 introduces a read-only annotation boundary that:

- annotates each ALT allele independently,
- performs exact REF/ALT database lookup without silently normalizing the query,
- collects overlapping genomic features through the Iteration 061 interval index,
- preserves explicit missing database attributes,
- copies complete database provenance into every emitted annotation,
- remains valid after source database/registry lifetime ends,
- rejects assembly mismatch, null databases, duplicate database identity and invalid variants,
- sorts database inputs by deterministic metadata identity before evaluation,
- enforces hard per-ALT output cardinality limits before appending results,
- preserves multi-allelic ALT order and ALT indices,
- remaps independently built ContigTable identities through the Iteration 061 query APIs.

## Provenance

Every allele and feature annotation carries:

- database ID,
- display name,
- version,
- schema version,
- assembly identity,
- source URI,
- source SHA-256.

The engine emits owning value objects. It does not expose references or pointers into a ReferenceDatabase.

## Allele annotations

Allele annotations are representation-exact. Iteration 056 remains the canonical normalization boundary. Annotation 2.0 does not modify or normalize input REF/ALT representations.

For a multi-allelic VariantRecord each ALT is queried independently and results remain attached to the original ALT index.

## Feature annotations

Generic genomic features imported by Iteration 061 from GFF3/GTF can be attached by interval overlap. Feature source, type, optional score, strand, phase, source ordinal and opaque attributes are preserved. Iteration 062 does not infer transcript consequence, protein effect, pathogenicity or ACMG classification from those fields.

## Determinism

Database input order must not affect output. Databases are evaluated using a stable identity key. Record and feature ordering inside each database remains the deterministic ordering established by Iteration 061.

## Safety bounds

When enabled, maximum allele annotations and maximum feature annotations per ALT must be positive. The engine checks capacity before appending a database's query result and fails with `std::length_error` when the configured bound would be exceeded.

## Non-goals

Iteration 062 does not implement:

- ACMG/AMP classification,
- clinical pathogenicity inference,
- transcript consequence prediction from sequence models,
- remote database downloading/updating,
- cohort statistics,
- association testing,
- GUI/workspace integration,
- persistent annotation caches.

## Validation

The iteration must pass:

- GCC Debug: 101/101 tests,
- GCC Release: 101/101 tests,
- Clang Debug: 101/101 tests,
- GCC ASan+UBSan: 101/101 tests,
- 1M Annotation 2.0 lookup benchmark,
- exact four-part Gemini review-package integrity gate,
- independent Gemini ACCEPT before freeze.
