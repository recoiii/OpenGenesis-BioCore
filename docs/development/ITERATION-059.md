# OpenGenesis-BioCore v0.3.0-dev — Iteration 059

## Title
Advanced Variant Filtering

## Accepted baseline

- Iteration 058 accepted ref: `accepted/iteration-058`
- Exact accepted baseline commit: `ad2fbbdabfa66df324dc649cc1de38e668fe2c52`

## Objective

Add a reusable, allele-aware advanced filtering layer on top of the Iteration 058 unified genotype result while preserving the strand and homopolymer evidence carried from Iteration 057. Filtering is deterministic, reason-preserving and fail-closed when an explicitly requested metric is unavailable.

## Scope

- Shared `VariantFilterEvidence` model for genotype/filter metrics.
- Per-ALT filtering for multi-allelic records without allele reordering or implicit genotype remapping.
- DP threshold using Iteration 058 genotype depth semantics.
- VAF threshold defined as ALT `AD / DP`, so ambiguous observations remain in DP but are not forced into AD.
- GQ threshold using Iteration 058 posterior-derived genotype quality.
- MQ threshold using VariantRecord INFO/MQ when available.
- Iteration 057 indel MQ fallback from alternate-supporting realignment reads when INFO/MQ is absent.
- Alternate-strand balance filtering with a configurable minimum evidence floor.
- Homopolymer-run filtering using retained Iteration 057 context evidence.
- Required-region and excluded-region masks over 0-based half-open genomic intervals.
- Deterministic, ordered filter reasons suitable for later report/export layers.
- Missing requested metrics fail closed with explicit `MISSING_*` reasons.
- Direct 057 -> 058 -> 059 integration fixture.
- Controlled 1M-decision filtering benchmark evidence.

## Non-goals

- Statistical cohort filters or population frequency filters.
- Annotation-dependent rules; generic prioritization begins in Iteration 060.
- Transcript/gene consequence filtering; Annotation 2.0 begins in Iteration 062.
- Somatic/tumor-normal strand models.
- Hard-coded clinical classification rules.
- Replacing the legacy v0.2 VCF QC plugin in this iteration.

## Normative invariants

1. Filters are evaluated per ALT allele; multi-allelic ALT ordering is never changed.
2. DP retains Iteration 058 semantics and includes ambiguous observations.
3. VAF is `ALT AD / DP`; ambiguous evidence never inflates ALT AD.
4. GQ is consumed from the Iteration 058 posterior-derived `GenotypeCall`; 059 does not recompute genotype probability.
5. Requested-but-missing DP/VAF/GQ/MQ/strand/homopolymer evidence fails closed with an explicit reason.
6. Strand filtering uses only alternate-supporting strand evidence retained by Iteration 057 and does not invent reference-strand counts.
7. Strand imbalance is not hard-filtered below `minimum_strand_observations`; below that floor evidence is treated as insufficient for a bias decision rather than one-sided bias.
8. Homopolymer evidence is preserved and filtered only when the policy explicitly requests a maximum run length.
9. Region masks use 0-based half-open overlap semantics and deterministic merged intervals.
10. Required-region and excluded-region reasons may coexist; filter reasons have deterministic evaluation order.
11. Iteration 057 evidence consistency is revalidated before adapting indel evidence into the filtering model.
12. Filtering does not mutate `VariantRecord`, `GenotypeCall`, strand evidence or homopolymer evidence.
13. Iteration 059 is policy/evaluation only; generic feature-expression/rule-engine work remains Iteration 060.

## Definition of Done

- DP/VAF/GQ/MQ tests cover pass, threshold failure and missing-metric fail-closed paths.
- Multi-allelic fixture proves per-ALT VAF evaluation without ALT reordering.
- Strand fixture covers balanced, biased and below-evidence-floor behavior.
- Homopolymer fixture proves policy-driven filtering using Iteration 057 context evidence.
- Region fixture covers merged masks, required regions, exclusions and half-open boundaries.
- Integration fixture executes real Iteration 057 candidate calling, Iteration 058 genotyping and Iteration 059 filtering without evidence translation hacks.
- 1M-decision benchmark reports deterministic pass/checksum counts, runtime and peak RSS.
- All Iteration 058 tests remain green.
- GCC Debug, GCC Release, Clang Debug and GCC ASan+UBSan pass the exact active CTest inventory.
- Gemini package contains exactly four complete Markdown parts and receives `VERDICT: ACCEPT` before freeze.
