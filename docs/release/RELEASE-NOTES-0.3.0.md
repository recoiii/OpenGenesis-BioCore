# OpenGenesis-BioCore v0.3.0 Release Notes

OpenGenesis-BioCore v0.3.0 is the small-variant analysis expansion release. It builds on the v0.2.0 platform
hardening baseline and adds canonical variant modeling, cohort analysis, reproducible export and truth-set
benchmarking while retaining the local-first execution and provenance model.

## Major changes since v0.2.0

- Canonical `VariantType` and record model for SNV, insertion, deletion, MNV, DELINS/complex and unknown alleles.
- VCF ingestion with canonical coordinate indexing and FASTA-backed normalization.
- Local realignment and indel candidate/context evidence.
- Unified probabilistic SNV+indel genotyping with genotype/allelic evidence representation.
- Advanced filters including depth/quality, strand-bias, homopolymer and regional evidence.
- Generic rule-based variant prioritization independent of annotation.
- Versioned local reference-database framework with checksum/provenance identities.
- Annotation 2.0 exact-allele lookup and GFF/GTF feature-overlap integration.
- Sparse multi-sample genotype matrices with shared/unique/cohort summaries.
- Case/control Fisher exact association, odds ratios, Woolf 95% confidence intervals and Benjamini-Hochberg FDR.
- Indexed variant-workspace query/detail domain with bounded windows, deterministic sorting and explicit missingness.
- Streaming TSV/CSV/JSON/site-level VCF export with producer, pipeline, reference and database provenance.
- Truth-set comparison with TP/FP/FN, precision/recall/F1, canonical type stratification and lossless biallelic
  genotype concordance.
- Pinned external GIAB HG002 GRCh38 v4.2.1 chr20 compatibility evidence plus a 250k synthetic benchmark.

## Compatibility and retained platform behavior

- The release keeps the local-only browser/security model, durable project storage, worker supervision,
  cancellation/retry semantics, managed-file integrity and plugin/pipeline execution contracts established by v0.2.0.
- Existing compatibility-sensitive machine identifiers such as `org.biocore.*`, `.biocore`, `share/biocore`,
  `X-BioCore-*` and `BioCore::*` are preserved.
- The bundled install remains eight native plugins and twelve pipelines.

## Scientific claim boundaries

The release does **not** claim functionality that is not present in the accepted source:

- Annotation 2.0 currently performs exact allele lookup and GFF/GTF feature overlap. Transcript/CDS/codon/protein
  consequence prediction is not implemented in this release.
- The GIAB gate verifies pinned external truth-set ingestion, confidence-region handling, canonical comparison and
  genotype-concordance semantics by self-comparison. It is not evidence of perfect end-to-end BioCore variant-caller
  accuracy on HG002 sequencing reads.
- Structural-variant/CNV calling, tumor-normal somatic calling, full GWAS, ACMG clinical classification and
  distributed/cloud execution are outside v0.3.0 scope.

## Validation policy

The release closes only when one exact Iteration 068 candidate passes Linux GCC Debug/Release, Clang Debug,
GCC ASan+UBSan, native Windows MSVC Debug/Release, install/package smoke, all retained benchmark/truth-set gates,
and independent Gemini plus Claude Code final review. No historical evidence may substitute for validation of the
exact candidate.

## Citation

The stable concept DOI is `10.5281/zenodo.22012037`. A version-specific v0.3.0 DOI is added only after the accepted
release source is deposited to Zenodo.
