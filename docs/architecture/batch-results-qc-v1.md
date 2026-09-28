# Batch Results & QC v1 — Iteration 085

Baseline: `accepted/iteration-084` / `e4335934a1872454f7f0a7519ed3c96a3ddc95f4`.

## Purpose

Iteration 085 adds a read-oriented results layer over the frozen batch plan, durable attempt
lineage and generated-output registry. It does not add another execution engine, a cross-sample
workflow node, or a new biological/statistical method.

The service exposes, per selected project sample:

- the latest durable Job/attempt state;
- verified output artifact links that are still current for that lineage;
- existing QC metrics already emitted by BioCore QC plugins;
- explicit QC comparability state;
- a guarded bridge from compatible terminal single-sample VCF outputs into the existing v0.3
  `VcfIngestionResult -> MultiSampleMatrix` path.

## Lineage-aware current results

A current result is not simply "all files from the latest Job". A resume attempt may execute only
a subset of the frozen plan while safely reusing verified output from its parent. Result discovery
therefore walks from the latest attempt backwards only through contiguous `resume` parents.

The walk stops at `initial` or `retry` boundaries. A full retry never inherits an older output as a
current result.

Within a resume chain, each newer attempt shadows every output port owned by a node in that
attempt's immutable `execution_node_ids`, even before the replacement file has been produced. This
prevents a node selected for recomputation from temporarily exposing its stale parent output as the
current result.

Artifact provenance is checked against the immutable frozen sample-plan node contract before it is
surfaced. Unknown nodes, ports, plugin/module identities, plugin versions or file types are rejected
from the current-result view and recorded as issues.

## Verified artifact reads

QC and VCF parsing never trusts database metadata alone. `FilesystemResultArtifactReader` composes
the existing artifact download verifier and requires:

- canonical flat `outputs/` path;
- regular non-symlink file;
- persisted size match;
- persisted SHA-256 availability and match.

After the existing verifier succeeds, the reader applies a bounded text-size limit, reads the file,
then hashes the bytes again and compares that hash with both the first verification and persisted
checksum. This second hash closes the ordinary read-after-verify mutation window for consumed bytes.

QC summaries are capped at 4 MiB and matrix VCF inputs at 512 MiB.

## Existing QC metrics only

Iteration 085 does not calculate new QC scores. It parses metrics already emitted by existing native
plugins:

- FASTA QC;
- FASTQ QC and trimming QC;
- alignment QC;
- VCF QC/filter.

FASTQ/FASTA/alignment TSV metric rows are consumed from the existing `table` output. Frozen trimming
parameter rows are removed from the metric set rather than misrepresented as observations. VCF-QC
metrics are consumed from the existing JSON `metrics` object and retain the existing summary schema
version.

Missing metric keys remain absent. JSON `null` remains explicit `null`. Neither condition is
converted to numeric zero.

## QC comparison contract

A QC comparison group is marked `comparable` only when every participating stable sample agrees on:

- exact plugin/module identity and plugin version;
- emitted metric schema version;
- frozen node parameter signature;
- reference signature for reference-sensitive QC;
- metric key/value-kind shape.

A version, parameter or reference mismatch is `incompatible`. Missing/unreadable QC, absent reference
evidence for reference-sensitive QC, unstable Jobs, or metric-shape gaps are `incomplete`.

Reference signatures are derived from frozen managed-file inputs carrying the `reference` sample
binding role. No comparison silently treats unknown reference provenance as equivalent.

## v0.3 matrix bridge

The bridge intentionally reuses the existing v0.3 parser and matrix builder; it does not introduce a
second VCF parser or genotype representation.

A sample is eligible only when:

1. its latest attempt is `completed`;
2. its frozen sample plan has exactly one terminal VCF output;
3. a current, verified artifact exists for that exact terminal node/port;
4. every eligible sample has the same terminal module/plugin version, frozen parameter signature and
   frozen reference signature;
5. the existing VCF ingestion contract accepts the file against the caller-supplied reference genome;
6. the VCF declares exactly one sample and that name exactly equals the project sample id.

Only then are the resulting `VcfIngestionResult` objects passed to the existing
`build_multi_sample_matrix` implementation. Nonconforming outputs are rejected; they are never
silently auto-merged.

## Persistence and boundaries

Iteration 085 is read-oriented and adds no schema migration. Project schema remains v15.

Explicitly out of scope:

- new biological scores or statistical methods;
- batch-wide fan-in execution nodes;
- mutation of generated artifacts or attempt history;
- provenance/report export (Iteration 086);
- the final project UI flow (Iteration 087);
- Windows/release closure evidence (Iteration 088).
