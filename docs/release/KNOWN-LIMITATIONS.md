# OpenGenesis-BioCore v0.5.0 Known Limitations

OpenGenesis-BioCore v0.5.0 adds Project & Sample Workspace integration while retaining the v0.3 scientific scope
and v0.4 Workflow Engine 2.0 contracts. The following boundaries are intentional.

## Project & sample workspace boundaries

- A running local server owns one validated current project root. Runtime hot-switching between project roots is not
  supported; create/open projects with the existing `biocore --init-project` / `biocore --serve` flow.
- Sample import and bindings are local project metadata. They are not a collaborative multi-user LIMS.
- Batch execution is selected samples × one frozen workflow template. Dynamic cross-sample DAG fan-in/fan-out is not
  introduced in v0.5.
- Orphan managed inputs and broken bindings are diagnostics; BioCore does not silently delete, repair or reassign them.
- Resume reuses only integrity-valid checkpoint/output evidence. Retry is a new attempt/Job and preserves historical
  failed/interrupted attempts rather than mutating them.

## Results and report boundary

- QC summaries reuse metrics already emitted by supported plugins. v0.5 does not invent a new biological QC score.
- QC from incompatible plugin/schema/parameter/reference contracts is marked incomplete/incompatible rather than
  silently compared.
- The multi-sample bridge accepts only compatible terminal single-sample VCF outputs and reuses the v0.3 VCF/matrix
  engine; it is not a new cohort caller.
- Batch JSON/HTML export is an integrity-verified manifest snapshot referencing project artifacts. It is not a full
  portable archive containing all large scientific inputs/outputs.

## Workflow/platform boundaries

- Workflow Engine 2.0 remains canonical DAG authoring/planning, typed binding, bounded branching, checkpoint/resume
  planning, durable state/recovery, reusable templates and browser workspaces. v0.5 does not add a second independent
  native multi-node dispatcher.
- The supported browser/network surface is localhost only. Remote serving, TLS termination and distributed/cloud
  execution are outside this release.
- Native plugins are trusted local executables; code signing, publisher trust and process sandboxing are not provided.
- Full filesystem TOCTOU elimination and explicit fsync/FlushFileBuffers guarantees are not claimed.

## Scientific boundaries

- Canonical small-variant scope remains SNV, insertion, deletion, MNV and DELINS/complex alleles. Structural-variant/
  CNV calling is outside scope.
- Tumor-normal somatic calling, full GWAS and ACMG clinical classification remain outside scope.
- Annotation 2.0 provides exact allele lookup and GFF/GTF feature overlap; transcript/CDS/codon/protein consequence
  prediction is not implemented.
- The pinned GIAB HG002 gate is a compatibility/provenance invariant, not a claim of perfect end-to-end caller
  accuracy on HG002 sequencing reads.
