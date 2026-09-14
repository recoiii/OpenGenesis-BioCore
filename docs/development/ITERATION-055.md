# Iteration 055 — Variant Model & Types

Status: CANDIDATE — independent Gemini acceptance required before freeze.

## Objective

Establish the shared C++20 variant-domain model used by the v0.3 normalization, calling, genotyping, filtering, reference-database and annotation layers. The model must be deterministic, type-safe, ploidy-aware and cheap on the common biallelic path without constraining later multi-allelic or polyploid work.

## Frozen invariants

1. Internal genomic coordinates are 0-based half-open `[start, end)`. VCF POS conversion is explicit and checked at the adapter boundary.
2. `VariantType` includes SNV, insertion, deletion, MNV, DELINS/COMPLEX and UNKNOWN from the first v0.3 iteration.
3. Adjacent substitutions can be represented as one MNV; unequal non-prefix substitutions can be represented as DELINS/COMPLEX. Downstream code must not force these into independent SNVs.
4. Common biallelic ALT, diploid GT, AD and PL values use inline-capacity storage; larger cardinalities spill to dynamic storage without changing the public model.
5. Genotypes are ploidy-aware. Missing allele index is `-1`; phasing is represented per allele separator rather than by one record-wide Boolean.
6. AD obeys VCF `Number=R` semantics. PL obeys `Number=G` cardinality derived from allele count and ploidy and canonical PL values have minimum zero.
7. INFO/FORMAT-compatible dynamic values preserve missing, Integer, Float, Character, String and Flag values plus vector cardinalities. AF, AC, AN, DP and MQ have fast-path fields in `VariantInfo`.
8. Contig lookup is amortized O(1). Canonical IDs are assigned only when a builder is finalized after deterministic sorting; input encounter order cannot change IDs.
9. Accession aliases are assembly-context data, not global assumptions. A GRCh37 table must not silently accept a GRCh38 accession unless explicitly registered.
10. No owning raw pointers are introduced. Review must also check lifetime safety, pointer arithmetic/aliasing and other undefined-behavior risks rather than treating every raw pointer as inherently invalid.

## Implementation

Shared domain additions:

- `inline_values.hpp`: small inline-capacity sequence with vector fallback.
- `variant_types.hpp/.cpp`: type enum, textual conversion and REF/ALT classification.
- `genotype.hpp/.cpp`: ploidy-aware GT/AD/DP/GQ/PL representation, Number=G calculation, validation and PL normalization.
- `contig_table.hpp/.cpp`: assembly-labelled deterministic contig/alias table with heterogeneous O(1)-average lookup.
- `variant_record.hpp/.cpp`: coordinates, typed INFO values, alleles, records and invariant validation.

The existing v0.2 variant caller remains behaviorally unchanged in Iteration 055. Migration of caller output into the shared model belongs to Iterations 056–058 so this foundation can be reviewed independently.

## Tests added

- `domain.variant_types`
  - SNV / insertion / deletion / MNV / DELINS classification
  - symbolic UNKNOWN behavior
  - 0-based ↔ VCF POS adapter checks
  - ALT inline-to-spill behavior and INFO cardinality validation
- `domain.genotype_representation`
  - haploid, diploid and triploid representation
  - missing/partially missing GT
  - per-separator phasing
  - AD Number=R
  - PL Number=G and minimum-zero normalization
  - common-path inline storage and polyploid spill
- `domain.contig_normalization`
  - chr/canonical/accession alias equivalence
  - assembly separation
  - deterministic IDs independent of insertion order
  - ambiguous alias rejection

## Memory benchmark

`biocore-variant-model-memory-benchmark` constructs and traverses exactly 1,000,000 simple biallelic `VariantRecord` objects. CI records:

- `sizeof(VariantRecord)`
- nominal contiguous bytes
- count of common-path ALT records that spilled to heap (must be zero)
- construction time
- traversal time
- process maximum resident set size from `/usr/bin/time -v`

The developer-side pre-CI measurement on the implementation source produced `sizeof(VariantRecord)=248`, zero ALT spill records and peak RSS about 243,712 KiB. This is preliminary evidence only; the GitHub Actions artifact is authoritative for review.

## Definition of Done

Iteration 055 can be accepted only when all of the following are true:

- full existing regression suite plus all three new tests passes under GCC Debug, GCC Release and Clang Debug;
- GCC ASan+UBSan passes the same complete CTest suite;
- test inventory is exactly 78 for this candidate;
- the 1M-record benchmark completes and reports zero common-path ALT spill records;
- binary development identity is `0.3.0-dev`;
- Gemini receives the exact four-part Markdown source package generated from the validated commit and returns `VERDICT: ACCEPT`;
- only after ACCEPT may an immutable `accepted/*` ref be created.

## Explicitly out of scope

- VCF ingestion/indexing/left-normalization (Iteration 056)
- local realignment and indel calling (Iteration 057)
- probabilistic genotype inference from read evidence (Iteration 058)
- advanced filtering, annotation databases, consequence annotation and UI integration
