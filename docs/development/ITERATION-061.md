# Iteration 061 — Reference Database Framework

## Baseline

- Accepted baseline: `accepted/iteration-060`
- Exact baseline SHA: `2b241b8bf7c546d0e75070a046fa2bed0088cd71`
- Development line: `v0.3.0-dev`

## Objective

Provide the versioned, provenance-preserving and assembly-aware local reference database foundation that Iteration 062 Annotation 2.0 can consume without embedding database-specific semantics in the annotation engine.

## Contract

Iteration 061 introduces:

- stable database identity, version and schema-version metadata,
- explicit GRCh37 / GRCh38 / custom assembly identity,
- lowercase SHA-256 source provenance and source URI retention,
- deterministic allele-specific reference records with generic string attributes,
- exact canonical REF/ALT lookup and half-open overlap lookup,
- deterministic coordinate indexing without duplicating `VariantRecord` ownership,
- a bounded local TSV ingestion format (`#OpenGenesis-BioCore-ReferenceDB\t1`),
- bounded GFF3 and GTF genomic-feature import with deterministic interval indexing,
- missing attribute preservation (`.` -> explicit missing value),
- a registry capable of holding multiple versions and assemblies of a database,
- stable database object addresses across subsequent registry insertions,
- fail-closed validation for malformed metadata, records, assemblies and TSV input.

## Assembly rules

Known assemblies use the existing `ReferenceAssembly` identity. Custom assemblies additionally require a stable non-empty `custom_id`. Database metadata must agree with the supplied `ContigTable` assembly. Queries must supply the exact database assembly identity and their source `ContigTable`. Numeric contig IDs are remapped through canonical contig names before lookup, so independently built tables with different contig subsets cannot silently cross-wire IDs. Assembly mismatches fail closed.

## Coordinate and allele rules

Coordinates remain 0-based half-open. REF must be uppercase A/C/G/T/N, non-symbolic and `VariantType::unknown`. ALT must differ from REF and either be uppercase sequence with the existing canonical `classify_variant()` type or a supported symbolic allele using `VariantType::unknown`.

Exact lookup is deliberately representation-exact. Iteration 056 remains the canonical normalization boundary; Iteration 061 does not silently normalize database or query alleles.

## Local TSV v1

Required header:

`contig\tstart\tend\tref\talt\trecord_id`

Optional columns use `attr:<key>`. Attribute values are opaque strings; `.` preserves an explicit missing value. Annotation interpretation is deferred to Iteration 062.

## GFF3 / GTF feature import

GFF3 and GTF are ingested as generic genomic feature records without transcript- or gene-consequence interpretation. External feature coordinates are 1-based inclusive and are converted exactly once to the internal 0-based half-open convention: `start_internal = start_external - 1`, `end_internal = end_external`.

The parser requires exactly nine tab-separated columns, skips comment lines beginning with `#`, validates finite scores, strand and phase, resolves contigs through the supplied `ContigTable`, preserves repeated GTF attributes (for example repeated `tag` keys), and bounds lines, feature counts and attribute cardinalities before unbounded allocation paths. GFF3 attribute values remain opaque strings; percent decoding and biological interpretation are intentionally deferred.

Feature overlap queries use a dedicated interval index and the same query-side canonical-contig-name remapping as allele records, preventing numeric `ContigId` cross-wiring between independently built tables.

## Determinism

Allele records are ordered by contig, start, end, REF, ALT and record ID. Feature records are ordered by contig, start, end, feature type, source, strand and source ordinal. Allele-record attributes are ordered by key; feature attributes are stable-sorted while preserving repeated keys. Registry metadata is ordered by database ID, version and assembly identity. Lookup results therefore have deterministic order.

## Safety bounds

Input lines, metadata strings, identifiers, attribute counts, columns and total record count are bounded before unbounded allocation paths. Duplicate record IDs, duplicate attribute keys, invalid assembly identities and malformed coordinates fail closed.

## Non-goals

Iteration 061 does not implement:

- transcript or gene consequence annotation,
- clinical classification or ACMG/AMP rules,
- network download/update clients,
- remote databases,
- database-specific field semantics,
- cohort statistics or association testing,
- persistent SQLite indexing of reference databases.

Those concerns remain outside this framework or belong to later iterations.

## Validation

The iteration must pass the full Linux compiler/sanitizer matrix, a 1M-query reference database benchmark, the exact four-part Gemini package integrity gate and independent Gemini ACCEPT before freezing.
