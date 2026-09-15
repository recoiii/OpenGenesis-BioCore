# Iteration 064 — Case/Control Association

## Baseline

- Accepted baseline: `accepted/iteration-063`
- Exact baseline SHA: `af3f80396156678e18df9f57d19cfa687a8c9eda`
- Development line: `v0.3.0-dev`

## Objective

Build a deterministic, sparse-safe case/control association layer directly on the accepted Iteration 063 multi-sample matrix without weakening its missingness semantics or rewriting genotype/allele state.

## Contract

Iteration 064 introduces:

- explicit case/control phenotype labels keyed by matrix sample ID,
- exactly one required phenotype label for every matrix sample,
- deterministic per-variant association output in matrix variant order,
- complete separation of unobserved cells, explicit no-calls, partial calls and complete calls,
- selected-ALT allelic 2×2 tables using only complete genotype calls,
- selected-ALT carrier/non-carrier 2×2 tables using only complete genotype calls,
- explicit sample/call-state accounting for auditability,
- odds-ratio state that distinguishes undefined, zero, finite and positive-infinity outcomes,
- two-sided Fisher exact p-values using fixed margins and probability-ordering semantics,
- hard Fisher support-state bounds to prevent unbounded exact-test work,
- explicit minimum complete-call gates per phenotype group,
- fail-closed validation of labels, phenotype enum values, cardinality limits and matrix invariants.

## Sparse and missingness semantics

Iteration 063 established that a missing sample/variant matrix observation means only that no source variant record was supplied for that sample/allele. Iteration 064 preserves that rule exactly.

For each variant and phenotype group, samples are partitioned into:

- `unobserved`: no matrix observation exists,
- `no_call`: a source record exists but GT is absent or fully missing,
- `partial_call`: GT contains both called and missing alleles,
- `complete_call`: all genotype allele slots are called.

Only `complete_call` observations contribute to allelic or carrier contingency tables. Unobserved, no-call and partial-call samples are never coerced to homozygous reference and never enter the statistical denominators.

## Allelic test

For a matrix column representing one exact selected ALT allele, complete genotype calls contribute:

- exposed = selected ALT dosage,
- unexposed = REF dosage + all other ALT dosage.

This retains the accepted Iteration 063 multi-allelic projection semantics. For a source genotype such as `1/2`, testing ALT1 counts one selected-ALT copy and one other-allele copy; testing ALT2 does the analogous projection. The original genotype is not modified.

The allelic contingency table is:

|            | selected ALT | other called alleles |
|------------|--------------|----------------------|
| cases      | case exposed | case unexposed       |
| controls   | control exposed | control unexposed |

## Carrier test

Among complete calls:

- a carrier has selected ALT dosage > 0,
- a non-carrier has selected ALT dosage == 0.

The carrier table therefore uses samples rather than allele copies and remains separate from the allelic table.

## Odds ratio semantics

`AssociationOddsRatio` avoids silently encoding undefined or infinite results as ordinary finite numbers:

- `undefined`: numerator and denominator cross-products are both zero,
- `zero`: numerator is zero and denominator is positive,
- `finite`: ordinary finite odds ratio,
- `positive_infinity`: denominator is zero and numerator is positive.

No continuity correction is silently applied.

## Fisher exact test

Iteration 064 implements a two-sided Fisher exact test for 2×2 tables with fixed margins. Tables with degenerate margins return p = 1.0. Non-degenerate support is enumerated in log-probability space using log-combination terms and log-sum-exp accumulation to avoid ordinary-probability underflow.

The two-sided definition sums tables whose probability is no greater than the observed table probability, with only a very small deterministic floating-point comparison tolerance.

A configurable positive `maximum_fisher_table_states` bound limits support enumeration before the loop begins. This provides a deterministic fail-closed CPU-work bound for unusually large tables.

## Phenotype labels

Every matrix sample must appear exactly once in the supplied label vector. Sample IDs and phenotype enum values are validated. Duplicate labels, missing labels, unknown sample IDs, invalid enum values, an all-case cohort or an all-control cohort fail closed.

Label input order has no effect on association results because labels are resolved by matrix sample ID.

## Result ordering and determinism

Association results are emitted exactly in accepted Iteration 063 matrix variant order. The engine does not reorder samples, variants or observations and does not depend on pointer identity or unordered-container iteration.

## Safety bounds

`CaseControlAssociationOptions` enforces positive limits for:

- maximum labels,
- maximum variants,
- minimum complete case calls required before p-values are emitted,
- minimum complete control calls required before p-values are emitted,
- maximum Fisher support states.

Unsigned contingency counts use checked addition before margin construction. Matrix dosage projections are revalidated for complete calls: REF + selected ALT + other ALT must equal genotype ploidy and no missing allele may remain.

## Integration boundaries

Iteration 064 consumes Iteration 063 `MultiSampleMatrix` directly. It does not mutate matrix observations, calls, genotypes, FORMAT fields, provenance or variant keys.

The end-to-end integration test exercises:

1. VCF ingestion,
2. Iteration 063 multi-sample matrix construction,
3. Iteration 062 exact annotation compatibility through `matrix.variant_record()`,
4. Iteration 064 case/control association from the same matrix.

## Non-goals

Iteration 064 intentionally does not implement:

- phenotype inference,
- covariate-adjusted logistic regression,
- population-stratification correction,
- multiple-testing correction / FDR,
- imputation,
- implicit hom-reference inference from VCF absence,
- haplotype association,
- gene-level burden testing,
- clinical interpretation / ACMG classification,
- GUI/workspace integration.

These belong to later analysis/reporting layers if adopted.

## Validation

The iteration must pass:

- GCC Debug: 110/110 tests,
- GCC Release: 110/110 tests,
- Clang Debug: 110/110 tests,
- GCC ASan+UBSan: 110/110 tests,
- 1M-observation case/control benchmark,
- exact four-part Gemini review-package integrity gate,
- independent Gemini ACCEPT before freeze.
