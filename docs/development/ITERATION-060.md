# OpenGenesis-BioCore v0.3.0-dev — Iteration 060

## Title
Variant Prioritization Engine

## Accepted baseline

- Iteration 059 accepted ref: `accepted/iteration-059`
- Exact accepted baseline commit: `0a376b5295f1f3b15e2eb9e68745a64896a348c9`

## Objective

Add a reusable, explainable and deterministic per-ALT prioritization/ranking layer above Iteration 059 filtering. Prioritization consumes existing variant, genotype/filter evidence and filter decisions without recomputing genotype likelihoods or changing hard-filter semantics.

## Scope

- Shared per-ALT `VariantPrioritizationContext`.
- Generic rules over DP, VAF, GQ, MQ, alternate-strand fraction, homopolymer run length, filter-pass state and variant type.
- Numeric comparison operators plus equality/inequality for boolean and variant-type features.
- Finite additive score deltas with a finite baseline score.
- Explicit missing-feature behavior: skip the rule or reject evaluation.
- One explainability trace entry per rule with observed value, match status and applied delta.
- Deterministic cumulative scoring in policy declaration order.
- Deterministic descending ranking with canonical genomic/allelic tie-breaking and input ordinal as the final tie-break.
- Real Iteration 057 -> 058 -> 059 -> 060 integration fixture.
- Controlled 1M-evaluation prioritization benchmark.

## Non-goals

- Annotation/reference-database lookup; reference database framework starts in Iteration 061.
- Transcript/gene consequence interpretation; Annotation 2.0 starts in Iteration 062.
- ACMG/AMP classification or hard-coded clinical interpretation.
- Population/cohort statistics or association testing.
- A user scripting language, arbitrary code execution or expression parser.
- Replacing Iteration 059 hard filtering.

## Normative invariants

1. Prioritization is per ALT allele and never reorders ALT alleles or remaps genotype allele indices.
2. Iteration 059 hard-filter decisions are consumed as evidence; Iteration 060 never recomputes or overrides them.
3. Rule order is preserved exactly in the explainability trace.
4. Duplicate or empty rule identifiers are rejected.
5. Baseline scores, score deltas and numeric expected values must be finite.
6. Numeric features accept numeric comparisons; boolean and variant-type features accept equality/inequality only.
7. Missing numeric features never silently satisfy a rule. Policy explicitly chooses `skip_rule` or `reject_evaluation`.
8. Every evaluated rule emits exactly one trace entry: `MATCHED`, `NOT_MATCHED` or `MISSING`.
9. Only matched rules contribute their configured score delta.
10. Score accumulation must remain finite; non-finite cumulative results fail closed.
11. Ranking is descending by score, then canonical contig/start/end/REF/ALT/ALT-index order, then stable input ordinal.
12. Ranking does not mutate the underlying `VariantRecord`, genotype evidence or Iteration 059 filter decision.
13. Iteration 060 contains no annotation database semantics; future annotation features may extend the context after Iterations 061/062.

## Definition of Done

- Rule validation tests cover invalid types, duplicate IDs and missing-feature behavior.
- Numeric, boolean and variant-type rules produce deterministic score and trace output.
- Context adaptation preserves 059 DP/VAF/GQ/MQ/filter state and retained 057 strand/homopolymer evidence.
- Ranking tests prove deterministic and idempotent score/genomic/allelic tie-breaking.
- Integration fixture executes real Iteration 057 candidate calling, Iteration 058 genotyping, Iteration 059 filtering and Iteration 060 prioritization.
- 1M-evaluation benchmark reports deterministic match/checksum counts, runtime and peak RSS.
- All Iteration 059 tests remain green.
- GCC Debug, GCC Release, Clang Debug and GCC ASan+UBSan pass the exact active CTest inventory.
- Gemini review package contains exactly four complete Markdown parts and receives `VERDICT: ACCEPT` before freeze.
