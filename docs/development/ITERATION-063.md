# Iteration 063 — Multi-Sample Matrix Engine

## Baseline

- Accepted baseline: `accepted/iteration-062`
- Exact baseline SHA: `8307a090832b42ecf6ec489268120cc621f96ef0`
- Development line: `v0.3.0-dev`

## Objective

Build a deterministic, assembly-aware and sparse multi-sample variant matrix suitable for cohort-level analysis while preserving the genotype semantics established by Iterations 055–062.

## Contract

Iteration 063 introduces a cohort-ready matrix boundary that:

- consumes canonical `VcfIngestionResult` sources without modifying them,
- supports one or many VCF sources and one or many samples per source,
- remaps independently constructed source `ContigTable` identities through canonical contig names,
- expands multi-allelic records into exact per-ALT matrix columns while preserving the original source ALT index,
- merges the same exact REF/ALT allele across biallelic and multi-allelic source representations,
- preserves the full `GenotypeCall` and typed extra FORMAT fields once per sample/record call,
- derives per-ALT REF, selected-ALT, other-ALT and missing dosages without mutating the source genotype,
- distinguishes an unobserved matrix cell from an explicit no-call,
- preserves partial-call state instead of coercing it to hom-reference or missing,
- provides deterministic sample rows, variant columns, row ranges and column-observation indexes,
- stores source/record provenance once and links calls by stable ordinal,
- exposes an exact biallelic `VariantRecord` projection for each matrix column so Iteration 062 annotation can be applied directly,
- rejects duplicate sample IDs, duplicate source IDs and duplicate sample/allele observations,
- enforces explicit source, sample, variant-allele and observation cardinality limits.

## Sparse semantics

A missing observation means only that the supplied source data contains no record for that sample/allele. Iteration 063 **must not infer homozygous reference from record absence**. An explicit source record whose GT field is absent or fully missing is retained as an observation with `no_call` state. Partially missing genotypes remain `partial_call`.

This distinction is mandatory for Iteration 064 case/control association because standard VCF record absence does not, by itself, prove a callable homozygous-reference genotype.

## Per-ALT projection

Each exact alternate allele becomes one matrix column identified by:

- target contig ID after canonical-name remapping,
- half-open locus start/end,
- REF sequence,
- selected ALT sequence,
- ALT symbolic flag,
- ALT variant type.

The selected ALT may originate from different original ALT positions in different sources. `source_alternate_index` preserves that original position. Genotype allele indices and Number=R/Number=G vectors remain in their original source-record coordinate system inside the retained `GenotypeCall`.

## Call pool

A source sample/record call is stored exactly once in `MultiSampleMatrixCall`, including:

- sample index,
- record-provenance index,
- `no_call` / `partial_call` / `complete_call` state,
- genotype-present flag,
- complete `GenotypeCall` (GT, phasing, DP, AD, GQ and PL),
- typed extra FORMAT fields.

Per-ALT observations reference the call by ordinal and carry only ALT-specific dosage projection fields.

## Determinism

- sources are evaluated in `source_id` order,
- samples are ordered by sample ID,
- variant columns are ordered by target contig ID, locus, REF and exact ALT identity,
- observations are ordered by `(sample_index, variant_index)`,
- column observation ordinals therefore enumerate samples deterministically,
- reversing caller source order must not change matrix semantics or stable ordinals.

## Assembly and contig safety

Every source assembly identity must exactly equal the requested matrix assembly identity. Source and target contig-table assembly enums must agree with that identity. Raw numeric source `ContigId` values are never compared directly to target IDs; source canonical names are resolved into the target table before matrix keys are formed.

## Safety bounds

`MultiSampleMatrixOptions` provides positive hard bounds for:

- sources,
- samples,
- unique exact variant alleles,
- sparse observations.

Limits are checked before append/growth. Invalid genotype calls, malformed variants, missing target contigs, null source pointers and cardinality mismatches fail closed.

## Non-goals

Iteration 063 does not implement:

- case/control labels or phenotype metadata,
- association statistics,
- implicit hom-reference inference from VCF absence,
- imputation,
- haplotype inference,
- genotype likelihood recomputation,
- annotation interpretation or ACMG classification,
- persistent on-disk cohort matrices,
- GUI/workspace integration.

Those concerns belong to Iteration 064 or later roadmap stages.

## Validation

The iteration must pass:

- GCC Debug: 105/105 tests,
- GCC Release: 105/105 tests,
- Clang Debug: 105/105 tests,
- GCC ASan+UBSan: 105/105 tests,
- 1M sparse matrix observation-scan benchmark,
- exact four-part Gemini review-package integrity gate,
- independent Gemini ACCEPT before freeze.
