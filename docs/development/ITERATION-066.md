# Iteration 066 — Reporting & Export Provenance

## Baseline

- Accepted baseline: `accepted/iteration-065`
- Exact baseline SHA: `8b24ee83be0308ce7d94ff765b5d24f8a0a433a9`
- Development line: `v0.3.0-dev`

## Objective

Provide deterministic, stream-oriented variant exports in TSV, CSV, JSON and VCF while carrying explicit pipeline, reference and annotation-database provenance without mutating or reinterpreting canonical variant semantics.

## Contract

Iteration 066 introduces a reporting/export boundary that:

- exports canonical workspace rows without SNV-only coercion,
- preserves SNV, MNV, INSERTION, DELETION and DELINS_COMPLEX identities,
- produces deterministic TSV, CSV, JSON and VCF representations,
- embeds producer, pipeline, reference and database provenance in every export family,
- requires an explicit reference identity and SHA-256 rather than inferring a reference from paths,
- automatically collects annotation database identity/version/schema/assembly/SHA-256 provenance from canonical Iteration 062 annotation results,
- rejects conflicting provenance for the same database id/version,
- never exposes annotation source URIs or private filesystem paths in portable exports,
- supports deterministic selected-variant subsets and rejects duplicate/out-of-range selections,
- writes exports directly to `std::ostream` so the complete output does not need to be materialized in memory,
- preserves missing values explicitly (`null` in JSON and `.` in tabular/VCF output),
- exposes Iteration 064 odds-ratio state, 95% CI, Fisher P and BH-adjusted q values without recomputation.

## VCF policy

The 066 VCF is a standards-oriented **site-level VCF**. It intentionally does not invent FORMAT/sample genotype columns. Iteration 063 preserves source-record allele-index coordinate systems, including multi-allelic calls; coercing those calls into a biallelic export could silently alter genotype meaning. Site-level VCF therefore exports the exact REF/ALT allele projection plus cohort/association summaries in INFO while leaving genotype reconstruction to a future explicitly lossless export contract.

Canonical internal coordinates remain 0-based half-open. VCF POS is emitted as 1-based as required by VCF. The test suite round-trips exported VCF back through Iteration 056 canonical ingestion and requires DELINS_COMPLEX identity to survive unchanged.

## Provenance

Portable export provenance includes:

- OpenGenesis-BioCore producer version,
- generation timestamp,
- pipeline id/version,
- optional job id/revision/attempt,
- reference id, assembly identity and SHA-256,
- annotation database id/display name/version/schema/assembly/SHA-256.

Source filesystem paths and annotation `source_uri` values are deliberately excluded from portable renderers.

## Determinism and safety

- selected variant indices are unique and range checked,
- selected subsets are coordinate ordered using the same stable variant identity tier used by the workspace,
- SHA-256 fields must contain exactly 64 hexadecimal characters,
- reference assembly provenance must equal the workspace assembly,
- database provenance conflicts fail closed,
- CSV quoting handles delimiter-bearing values,
- VCF identifiers/contigs are validated or safely degraded where the VCF grammar cannot represent an arbitrary workspace display string,
- stream failures throw rather than silently producing a partial success result.

## Validation target

Iteration 066 advances the active regression floor from 115 to 120 CTest entries:

1. `integration.variant_export_provenance`
2. `integration.variant_export_tabular`
3. `integration.variant_export_json`
4. `integration.variant_export_vcf`
5. `integration.variant_export_validation`

The validation matrix remains GCC Debug, GCC Release, Clang Debug and GCC ASan+UBSan. Existing case/control and workspace benchmarks remain regression gates. A new 100,000-variant streaming TSV export benchmark is added as Iteration 066 evidence.

The iteration is not accepted or frozen until the exact candidate passes the complete matrix, produces the exact four-part Gemini package, and receives an independent Gemini `ACCEPT` verdict.
