# OpenGenesis-BioCore v0.3.0-dev — Iteration 057

## Title
Local Re-alignment & Indel Candidate Calling

## Accepted baseline

- Iteration 056 accepted ref: `accepted/iteration-056`
- Exact accepted baseline commit: `0ab2ceed8166213e98c162f779351f0e33044cd2`

## Objective

Add a reusable C++20 indel-candidate engine on top of the Iteration 056 canonical reference/normalization boundary. Candidate seeds are extracted from gapped read alignments, normalized immediately, then re-evaluated against reference and alternate local haplotypes. The output is genotype-neutral evidence for Iteration 058 rather than a premature genotype call.

## Scope

- Shared `ReadAlignment` + typed CIGAR operation model for candidate calling.
- Strict CIGAR parsing with overflow/zero-length rejection.
- Fail-closed read/reference-span validation.
- CIGAR insertion and deletion seed extraction with base-quality gating.
- Correct VCF-style anchoring for insertions/deletions, including contig-start edge cases.
- Immediate reuse of Iteration 056 `normalize_variant` and FASTA REF validation for every seed.
- Deterministic aggregation of equivalent normalized seeds.
- Per-read seed de-duplication so one read cannot inflate support for the same canonical candidate.
- Bounded semi-global local haplotype re-alignment against REF and ALT windows.
- Separate reference / alternate / ambiguous re-alignment support counts.
- Forward/reverse strand evidence for both seed and alternate-supporting reads.
- Homopolymer context evidence (`run_length`, `base`) retained on every emitted candidate.
- Candidate output remains genotype-neutral; read support is not misrepresented as VCF AC/AN genotype statistics.
- Deterministic candidate ordering.
- Canonical-boundary regression proving BioCore-produced indels can be represented as VCF and re-ingested by the exact Iteration 056 boundary without caller-specific repair.
- Controlled 10k-read realignment benchmark evidence.

## Non-goals

- Probabilistic genotype likelihoods or genotype assignment (Iteration 058).
- Final strand-bias/homopolymer filtering policy (Iteration 059).
- Structural-variant or breakend calling.
- Somatic/tumor-normal specialization.
- Replacing all legacy SNV caller parsing/output code in this iteration.
- UI/workspace integration (Iteration 065).

## Normative invariants

1. Iteration 057 candidate output is always a valid Iteration 055 `VariantRecord` and passes Iteration 056 FASTA-backed reference validation.
2. Every insertion/deletion seed is normalized through the exact Iteration 056 normalization API before aggregation.
3. Equivalent repeat-context indels collapse to one deterministic canonical candidate.
4. A single read contributes at most one seed-support unit to a given canonical candidate.
5. Duplicate, secondary, supplementary, QC-failed, low-MAPQ, and globally low-quality reads cannot contribute candidate or realignment support.
6. CIGAR read/reference consumption must exactly fit sequence and reference bounds.
7. Candidate indel length, read length, and realignment flank are explicitly safety-bounded.
8. Re-alignment is genotype-neutral: it classifies each informative read as REF-supporting, ALT-supporting, or ambiguous only.
9. Forward and reverse support counts are retained independently; they are never collapsed before Iteration 059.
10. Homopolymer context is retained independently; no hard homopolymer filtering occurs in Iteration 057.
11. Read evidence is never encoded into VCF `AC`/`AN`, whose semantics are genotype/sample allele counts.
12. Candidate ordering is deterministic by canonical contig/start/REF/ALT.
13. The candidate engine owns no raw-pointer read storage beyond the duration of a call and returns value-semantic records/evidence.
14. BioCore-produced candidate alleles must survive Iteration 056 VCF ingestion/normalization unchanged without caller-specific repair.

## Definition of Done

- Insertion fixture proves canonical repeat-context normalization, forward/reverse seed evidence, forward/reverse ALT support, REF support and homopolymer evidence.
- Deletion fixture proves local haplotype re-alignment and filtered-read exclusion.
- Canonical-boundary fixture serializes a BioCore-produced indel to VCF and re-ingests it through Iteration 056 with unchanged canonical locus/REF/ALT.
- Malformed/zero-length CIGAR input fails closed.
- 10k-read evidence benchmark reports deterministic seed/candidate counts, runtime and peak RSS.
- All Iteration 056 tests remain green.
- GCC Debug, GCC Release, Clang Debug and GCC ASan+UBSan pass the exact active CTest inventory.
- Gemini package contains exactly four bounded Markdown parts with every Iteration-056-relative changed file embedded exactly once and without truncation.

## Acceptance rule

Iteration 057 may be frozen only after independent Gemini review returns `VERDICT: ACCEPT`. A rejection keeps the same iteration number and requires revision plus a full CI rerun.
