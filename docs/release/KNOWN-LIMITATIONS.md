# OpenGenesis-BioCore v0.4.0 Known Limitations

OpenGenesis-BioCore v0.4.0 adds the Workflow Engine 2.0 foundation while retaining the v0.3 scientific scope.
The following boundaries are intentional.

## Workflow Engine 2.0 boundaries

- Workflow Engine 2.0 provides canonical DAG authoring/planning, typed binding, bounded declarative branching,
  checkpoint/retry/resume planning, durable workflow state/recovery, reusable templates, a browser Builder and a
  persisted Execution Workspace.
- The v0.4 Execution Workspace projects authoritative persisted state and resume decisions; it does **not** introduce
  a second independent native multi-node dispatcher alongside the existing Job/Pipeline runtime.
- Conditional execution is bounded and declarative. Arbitrary user scripting/expression execution is not supported.
- Checkpoint reuse depends on declared output-set completeness plus project-root, file-size and SHA-256 verification.
  It is not a native plugin process snapshot.
- Reusable templates are exact-version immutable catalog snapshots. Dynamic remote registries, version ranges and
  hot reload are outside v0.4.
- Workflow drafts are authored in the browser but are not a general collaborative/cloud document model.

## Platform and execution

- The supported browser/network surface is localhost only. Remote serving, TLS termination and distributed/cloud
  execution are outside this release.
- Native plugins are trusted local executables; code signing, publisher trust and process sandboxing are not provided.
- Full filesystem TOCTOU elimination and explicit fsync/FlushFileBuffers guarantees are not claimed.

## Alignment and file-processing boundaries

- The bundled native alignment foundation remains an exhaustive ungapped/Hamming implementation. It is not a
  whole-human-genome-scale indexed aligner and does not provide splice-aware or affine-gap alignment.
- SAM/BAM QC and coverage/depth do not create, sort, index or rewrite BAM files and do not parse CRAM.
- Some legacy bundled workflow components predate the canonical variant layer; domain capabilities do not imply that
  every capability is exposed through every older plugin or pipeline.

## Variant and annotation boundaries

- The scientific scope remains canonical small variants: SNV, insertion, deletion, MNV and DELINS/complex alleles.
  Structural-variant/CNV calling is outside scope.
- Annotation 2.0 provides exact allele lookup and GFF/GTF feature overlap. Transcript/CDS/codon/protein consequence
  prediction is not implemented.
- Tumor-normal somatic calling, full GWAS and ACMG clinical classification remain outside scope.

## Benchmark interpretation

- The pinned GIAB HG002 GRCh38 v4.2.1 chr20 gate validates external truth-data provenance, confidence-region handling,
  canonical ingestion/comparison and genotype concordance by comparing the fixture with itself. It is a compatibility
  invariant, not a claim of perfect end-to-end BioCore caller accuracy.
- The truth-set engine uses exact canonical allele identity after upstream normalization and is not a replacement for
  a haplotype-aware benchmarking suite such as hap.py for complex representation-equivalence cases.
- Genotype concordance is calculated only for complete losslessly comparable biallelic calls.

## Export boundary

- Site-level VCF export does not invent cohort sample genotypes when a lossless sample-level mapping is unavailable.
