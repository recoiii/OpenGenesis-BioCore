# OpenGenesis-BioCore v0.6.0 Release Notes

## Cohort Analysis Workspace

v0.6.0 extends the accepted Project & Sample Workspace with a reproducible project-scoped cohort analysis layer.

- Revision-controlled cohort/group registry with validation and immutable historical revisions.
- Deterministic artifact selection from verified sample/batch outputs.
- Reference/ploidy/contract compatibility gates before cohort matrix construction.
- Canonical multi-sample genotype matrix reuse with explicit missingness semantics.
- Frozen analysis approval snapshots that bind groups, selected artifacts, QC exclusions and reference identity.
- Case/control analysis reusing the existing Fisher exact, odds-ratio, 95% CI and Benjamini-Hochberg FDR logic.
- Durable cohort execution through the existing scheduler/plugin protocol, with recovery/retry lineage and
  verified result-manifest completion.
- Project-scoped cohort result browsing, carrier views, annotation links and JSON/CSV/TSV/HTML export.
- Integrated localhost Cohort Workspace and known-result end-to-end validation.

## Compatibility and architecture

v0.6.0 does not replace the v0.3 scientific engine, v0.4 Workflow Engine 2.0 or v0.5 project/sample/batch services.
It composes those accepted layers. Missing genotypes are not reference genotypes; incompatible-reference results
are not silently merged; result filtering does not recompute the frozen statistical universe.

Release inventory is project schema v18, Worker Protocol v2, nine native plugins, thirteen analysis pipelines and
one workflow template. The release remains local-first and localhost-only.

## Validation policy

The release closes only when one exact Iteration 098 candidate passes 359/359 CTests in Linux GCC Debug/Release,
Clang Debug and GCC ASan+UBSan; retained compatibility/scientific/cohort gates; native Windows MSVC Debug/Release;
clean install and portable-package smoke; and independent Gemini plus Claude final review. All evidence must bind
to the same candidate source archive and SHA-256 identity.
