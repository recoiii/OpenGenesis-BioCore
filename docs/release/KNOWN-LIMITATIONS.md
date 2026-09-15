# OpenGenesis-BioCore v0.3.0 Known Limitations

OpenGenesis-BioCore v0.3.0 materially expands small-variant analysis, but the following boundaries are intentional
and should be considered when interpreting outputs or designing workflows.

## Platform and execution

- The supported browser/network surface is localhost only. Remote serving, TLS termination and distributed/cloud
  execution are outside this release.
- Native plugins are trusted local executables; code signing, publisher trust and process sandboxing are not yet
  provided.
- Interrupted jobs use conservative recovery/retry semantics rather than checkpoint/resume of a native plugin.
- Full filesystem TOCTOU elimination and explicit fsync/FlushFileBuffers guarantees are not claimed.

## Alignment and file-processing boundaries

- The bundled native alignment foundation remains an exhaustive ungapped/Hamming implementation. It is not a
  whole-human-genome-scale indexed aligner and does not provide splice-aware or affine-gap alignment.
- SAM/BAM QC and coverage/depth do not create, sort, index or rewrite BAM files and do not parse CRAM.
- Some legacy bundled workflow components predate the v0.3 canonical variant layer; v0.3 domain capabilities do
  not imply that every capability is exposed through every older plugin or browser workflow.

## Variant and annotation boundaries

- v0.3.0 focuses on canonical small variants: SNV, insertion, deletion, MNV and DELINS/complex alleles.
  Structural-variant/CNV calling is outside scope.
- The current Annotation 2.0 implementation provides exact allele lookup and GFF/GTF feature overlap. It does not
  implement transcript/CDS/codon/protein consequence prediction, and release outputs must not imply otherwise.
- The indexed Variant Analysis Workspace contract provides bounded query/detail domain behavior; this release does
  not claim that every workspace capability is exposed through a dedicated new browser page.
- Tumor-normal somatic calling, full GWAS and ACMG clinical classification are outside scope.

## Benchmark interpretation

- The pinned GIAB HG002 GRCh38 v4.2.1 chr20 gate validates external truth-data provenance, confidence-region
  handling, canonical ingestion/comparison and genotype concordance by comparing the fixture with itself.
  Precision/recall/F1 = 1 in that gate is therefore a compatibility invariant, **not** a claim of perfect BioCore
  caller accuracy on HG002 sequencing reads.
- The truth-set engine uses exact canonical allele identity after upstream normalization; it is not a replacement
  for a haplotype-aware benchmarking suite such as hap.py in complex representation-equivalence cases.
- Genotype concordance is calculated only for complete losslessly comparable biallelic calls. Multi-allelic,
  partially missing and uncalled genotypes are excluded from the comparable denominator rather than coerced.

## Export boundary

- Site-level VCF export intentionally does not invent cohort sample genotypes when a lossless sample-level mapping
  is unavailable. Cohort and association statistics are represented through documented INFO fields instead.
