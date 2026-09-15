# Iteration 067 — Benchmarking & Truth Sets

## Baseline

- Accepted baseline: `accepted/iteration-066`
- Exact baseline SHA: `fa9d76b3212478270cadff7fdff552c89562f830`
- Development line: `v0.3.0-dev`

## Objective

Provide a deterministic truth-set benchmarking boundary for canonical small variants, with synthetic correctness/performance fixtures and independently sourced GIAB data as an external truth-set compatibility gate.

## Contract

Iteration 067 introduces `BenchmarkVariantObservation` as a benchmark-only canonical comparison projection. It preserves contig, 0-based half-open interval, REF, ALT, canonical `VariantType`, symbolic status, and an optional lossless genotype summary. The official Iteration 056 adapter consumes `VcfIngestionResult`; comparison code does not renormalize, repair, or reinterpret canonical variants.

Variant identity is exact `(contig, start, end, REF, ALT, symbolic)`. Inputs are sorted by index rather than copied, duplicate identities fail closed, and matching is deterministic. Metrics are:

- true positive: exact truth/candidate allele identity match,
- false positive: candidate allele without an exact truth match,
- false negative: truth allele without an exact candidate match,
- precision = TP / (TP + FP), when defined,
- recall = TP / (TP + FN), when defined,
- F1 = harmonic mean of precision and recall, when both are defined,
- per-type metrics for SNV, insertion, deletion, MNV, DELINS_COMPLEX and UNKNOWN.

## Genotype concordance

Genotype concordance is deliberately unphased allele-dosage concordance. A genotype is comparable only when both sides expose a lossless complete biallelic genotype projection. Multi-allelic, partial, missing or otherwise lossy genotypes are excluded from the concordance denominator rather than coerced. Concordance is `concordant / comparable` when at least one genotype is comparable.

## Synthetic truth sets

The correctness suite explicitly exercises TP/FP/FN accounting, precision/recall/F1, genotype concordance/missingness, all canonical non-UNKNOWN small-variant topologies, Iteration 056 VCF adaptation and fail-closed validation. A 250,000-variant synthetic performance benchmark requires exactly 245,000 TP, 5,000 FP and 5,000 FN.

## External GIAB truth data

The external gate uses the real HG002 GRCh38 GIAB v4.2.1 chr20 benchmark VCF and confidence BED mirrored in the Google DeepVariant repository at pinned commit `45f2627504c59785ea2b88d0256a2ec347bce7b4`.

Pinned mirror object identities are recorded in `benchmarks/giab/HG002-GRCh38-v4.2.1-chr20.json`. CI downloads both files from that exact commit, verifies their Git blob SHA-1 identities, records SHA-256 digests as evidence, decompresses the VCF, restricts observations to confidence BED intervals, and runs the same truth-set benchmark engine. The external gate is intentionally described as **truth-set compatibility evidence**, not as an end-to-end BioCore caller accuracy claim: no HG002 read-call pipeline is fabricated for this iteration.

The GIAB gate must contain at least 1,000 benchmark alleles and exercise SNV, insertion and deletion strata. The external truth set self-comparison must produce exact TP coverage with zero FP/FN and genotype self-concordance for all losslessly comparable biallelic calls.

## Performance and safety

- Comparison complexity is `O(n log n + m log m)` for deterministic index sorting followed by a linear merge.
- Full observation collections are not duplicated for sorting.
- Count types are 64-bit.
- Invalid intervals, allele/type mismatches, invalid genotype dosage and duplicate identities fail closed.
- Existing 1M case/control, 100k workspace and 100k streaming-export benchmarks remain regression gates.

## Validation target

Iteration 067 advances the CTest floor from 120 to 125:

1. `domain.truth_set_benchmark_metrics`
2. `domain.truth_set_benchmark_genotype`
3. `domain.truth_set_benchmark_stratification`
4. `domain.truth_set_benchmark_vcf_adapter`
5. `domain.truth_set_benchmark_validation`

The compiler/sanitizer matrix remains GCC Debug, GCC Release, Clang Debug and GCC ASan+UBSan. Iteration 067 is not accepted or frozen until the exact candidate passes all gates, produces the exact four-part Gemini review package, and receives an independent Gemini `ACCEPT` verdict.
