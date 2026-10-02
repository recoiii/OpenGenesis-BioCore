# OpenGenesis-BioCore v0.6.0 Known Limitations

OpenGenesis-BioCore v0.6.0 adds a project-scoped cohort analysis workspace while retaining the v0.3 scientific
scope, v0.4 Workflow Engine 2.0 contracts and v0.5 Project & Sample Workspace.

## Cohort boundaries

- Cohort analyses are project-local and bind immutable group, selected-artifact, reference and snapshot identity.
- Missing genotype observations remain missing; they are not silently treated as homozygous reference.
- Reference-incompatible or contract-incompatible sample results are rejected from a shared cohort analysis rather
  than silently merged.
- Case/control analysis reuses the accepted canonical matrix and Fisher exact / odds-ratio / 95% CI / BH-FDR logic;
  v0.6 does not add a second statistics engine.
- The cohort workspace is not a full GWAS platform, pedigree-analysis suite or clinical interpretation system.
- Result filtering/views do not recompute the frozen statistical universe.

## Project & workflow boundaries

- A running local server owns one validated current project root; runtime hot-switching is not supported.
- Sample and cohort metadata are local project state, not a collaborative multi-user LIMS.
- Native plugins are trusted local executables; code signing, publisher trust and process sandboxing are not provided.
- Remote serving, TLS termination and distributed/cloud execution remain outside this release.
- Full filesystem TOCTOU elimination and explicit fsync/FlushFileBuffers guarantees are not claimed.

## Scientific boundaries

- Canonical small-variant scope remains SNV, insertion, deletion, MNV and DELINS/complex alleles. Structural-variant
  and CNV calling are outside scope.
- Tumor-normal somatic calling, full GWAS and ACMG clinical classification remain outside scope.
- Annotation 2.0 provides exact allele lookup and GFF/GTF feature overlap; transcript/CDS/codon/protein consequence
  prediction is not implemented.
- The pinned GIAB HG002 gate is a compatibility/provenance invariant, not a claim of perfect end-to-end caller
  accuracy on HG002 sequencing reads.
