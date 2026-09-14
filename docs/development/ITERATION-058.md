# OpenGenesis-BioCore v0.3.0-dev — Iteration 058

## Title
Unified Probabilistic Genotyping

## Accepted baseline

- Iteration 057 accepted ref: `accepted/iteration-057`
- Exact accepted baseline commit: `85e964c24de434ecee022b5d8ae7041f124f3181`

## Objective

Add one reusable probabilistic/Bayesian genotype engine for SNVs and indels. The engine consumes allele-level likelihood evidence rather than variant-type-specific caller state, enumerates genotype hypotheses in canonical VCF Number=G order, computes genotype likelihoods and posterior probabilities, and emits the shared Iteration 055 `GenotypeCall` representation with GT/AD/DP/GQ/PL.

## Scope

- Variant-type-agnostic `GenotypeEvidence` with counted likelihood buckets.
- One shared likelihood/posterior path for SNVs and indels.
- Canonical VCF genotype ordering for arbitrary supported allele count and ploidy.
- Uniform genotype priors by default with optional caller-supplied Number=G priors.
- Numerically stable posterior normalization in log10 space.
- PL emitted from raw genotype likelihoods, normalized to minimum zero.
- GQ emitted from posterior probability of the selected genotype.
- GT selected from maximum posterior probability with deterministic VCF-order tie breaking.
- DP counts all classified/ambiguous observations represented by evidence buckets.
- AD preserves allele-assigned observations only; ambiguous observations are not falsely assigned to an allele.
- Arbitrary ploidy support within an explicit maximum genotype-state safety bound.
- Iteration 057 indel adapter using local-realignment REF/ALT/ambiguous evidence only.
- Strict consistency check that Iteration 057 informative realignments equal REF + ALT + ambiguous classifications.
- SNV and indel fixtures proving identical evidence produces identical likelihood/genotype behavior through the same engine.
- Controlled 100k-call benchmark evidence.

## Probability model

Each evidence bucket contains a count and one conditional likelihood per REF/ALT allele. For genotype `G` with ploidy `P`, the observation probability is the average allele likelihood over the allele copies in `G`. Independent observations accumulate in log10 likelihood space. Posterior mass is proportional to genotype likelihood multiplied by the configured genotype prior.

`make_count_genotype_evidence` provides a deterministic count-based adapter using a caller-supplied error probability: the observed allele receives probability `1-e`, off-target alleles share `e`, and ambiguous observations receive equal likelihood under every allele.

## Normative invariants

1. SNVs and indels may not use separate genotype decision algorithms.
2. Genotype likelihood arrays are emitted in canonical VCF Number=G order.
3. `genotype_likelihood_count(allele_count, ploidy)` and enumerated genotype cardinality must agree exactly.
4. Evidence allele cardinality must exactly match VariantRecord REF + ALT cardinality.
5. Each likelihood is a finite probability in `[0,1]`, and every evidence bucket has positive likelihood mass.
6. Zero-observation genotyping is rejected rather than silently producing a prior-only call.
7. Genotype priors are optional; when supplied they must have exact Number=G cardinality, finite non-negative mass, and positive total mass.
8. Priors may change the posterior-selected GT/GQ but may not alter raw genotype likelihoods or PL.
9. GT is unphased in Iteration 058; phasing is not inferred from unphased pileup/realignment evidence.
10. PL is likelihood-derived and normalized independently of posterior priors.
11. GQ is posterior-derived and safety-capped.
12. AD is Number=R and excludes ambiguous evidence rather than inventing allele assignment.
13. DP includes every represented evidence observation, including ambiguous observations.
14. Genotype state enumeration is explicitly safety-bounded to prevent combinatorial allocation explosions.
15. Iteration 057 strand and homopolymer evidence remains untouched for Iteration 059; genotyping does not hard-filter those contexts.
16. Iteration 057 seed observations are not double-counted as genotype evidence; the adapter consumes local-realignment classifications.

## Non-goals

- Haplotype phasing or phase-set inference.
- Population/cohort allele-frequency estimation.
- Pedigree-aware priors.
- Strand-bias or homopolymer filtering policy (Iteration 059).
- Somatic/tumor-normal genotype models.
- Structural-variant genotype models.
- Rewriting the legacy SNV plugin UI/output surface in this iteration; the shared domain engine is the canonical genotyping abstraction for subsequent variant workflows.

## Definition of Done

- Canonical VCF ordering tests cover diploid biallelic and triploid triallelic Number=G ordering.
- Strong REF-only, balanced REF/ALT, and ALT-only SNV evidence produce expected 0/0, 0/1, and 1/1 calls.
- Posterior probabilities normalize to one and GQ is derived from posterior confidence.
- PL minimum is zero and PL ordering matches canonical VCF genotype ordering.
- Supplied priors demonstrably change posterior calls without changing raw likelihoods.
- Triploid/multiallelic output validates through the existing Iteration 055 genotype model.
- A real Iteration 057 indel candidate is genotyped through the same engine used by the SNV fixture.
- Identical SNV/indel evidence produces identical GT/PL behavior.
- Invalid evidence cardinality, invalid prior cardinality, zero observations and genotype-state explosion fail closed.
- 100k-call benchmark reports deterministic call/checksum, runtime and peak RSS.
- All Iteration 057 tests remain green.
- GCC Debug, GCC Release, Clang Debug and GCC ASan+UBSan pass the exact active CTest inventory.
- Gemini review package contains exactly four complete Markdown parts and receives `VERDICT: ACCEPT` before freeze.
