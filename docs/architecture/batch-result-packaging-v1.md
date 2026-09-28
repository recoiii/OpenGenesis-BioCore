# Batch Result Packaging v1 — Iteration 086

Baseline: `accepted/iteration-085` / `5997d5f7f00ba23c3b5833f43c0e8dd7c101794f`.

## Purpose

Iteration 086 turns the read-only result/QC aggregation model from Iteration 085 into two export
surfaces for a frozen approved batch plan:

- a machine-readable JSON export manifest;
- a human-readable self-contained HTML report.

The package is a **verified manifest snapshot**, not a new analysis run and not a ZIP/TAR archive.
Scientific output bytes stay in their managed project locations. The manifest records only
project-relative paths plus immutable provenance and freshly verified SHA-256 values.

## Trust boundary

`BatchResultPackageService` first asks `BatchResultsService` for the current lineage-aware result
view. It then re-resolves every current artifact through `IManagedFileRepository` and requires the
persisted artifact identity to still equal the snapshot:

- managed-file id;
- Job id;
- attempt number/mode carried by the result link;
- step id and output port;
- module id and plugin version;
- file type;
- project-relative output path;
- persisted size;
- persisted SHA-256 identity.

The physical file is then re-verified with the existing `IArtifactContentAccess` contract. Export
fails closed when the file disappeared, became unsafe/non-regular, changed size, lacks a usable
SHA-256, changed checksum, or cannot be read.

This second export-time check is intentionally independent of the earlier Iteration 085 parse-time
checks. A result that was valid during aggregation is not assumed to remain valid later when a user
requests an export.

## Snapshot semantics

The package records:

- schema version;
- OpenGenesis-BioCore producer version;
- export timestamp;
- plan/project/template identity;
- every sample disposition and current result state;
- latest Job/attempt identity;
- current lineage-aware artifact links;
- fresh verified SHA-256 per current artifact;
- existing QC summaries and value kinds, including explicit null;
- QC comparability decisions and reasons;
- aggregation issues already reported by Iteration 085.

`stableSnapshot` is true only when every included sample has a persisted terminal latest Job.
Excluded samples are stable by definition because they have no execution responsibility.

`completeResults` is true only when every included sample's latest Job is `completed`. It does not
invent QC completeness or convert an `incomplete` comparison to a successful one; the QC comparison
state and issues remain independently visible in the manifest/report.

## Determinism

For a fixed `BatchResultPackage` object, JSON and HTML rendering are deterministic. Artifact
verification entries are sorted by sample id, step id, output port, Job id, and managed-file id.
The export timestamp is deliberately provenance: two separately requested exports may differ in
`generatedAtUtc`, while the ordered scientific/provenance payload remains stable for the same
snapshot.

No absolute filesystem path is serialized. Only normalized project-relative paths from the verified
managed artifact model appear in JSON or HTML.

## JSON manifest

`render_batch_result_package_manifest_json` emits schema v1 with:

- producer identity;
- snapshot flags;
- plan identity;
- sample execution provenance;
- current artifact provenance with persisted and freshly verified SHA-256;
- existing QC metrics without value transformation;
- QC comparison states;
- result aggregation issues.

Missing metric values stay JSON `null`; they are never rendered as `0`.

## HTML report

`render_batch_result_package_html` renders the same `BatchResultPackage` object. It does not query
storage again or calculate different scientific data. User-controlled text is HTML escaped.

The local API serves HTML with:

- `Cache-Control: no-store`;
- `X-Content-Type-Options: nosniff`;
- restrictive CSP: `default-src 'none'; style-src 'unsafe-inline'; frame-ancestors 'none'`.

The report contains only inline CSS and no script, remote asset, or external dependency.

## Local API

Authenticated endpoints:

- `GET /api/v1/batches/<plan-id>/export-manifest.json`
- `GET /api/v1/batches/<plan-id>/report.html`

They reuse the existing localhost bearer/browser-session authorization boundary. When batch result
packaging is unavailable the API returns `503`. Export-time artifact identity/integrity conflicts
return `409` and no partial manifest/report body is produced.

## Explicit non-goals

Iteration 086 does not:

- create a ZIP/TAR container or duplicate scientific output bytes;
- mutate Jobs, plans, attempts, managed files, or SQLite schema;
- add a new biological score, QC metric, statistical method, or variant parser;
- change Iteration 085 lineage/QC/matrix semantics;
- sign exports cryptographically or provide PKI/notarization;
- perform release-level native Windows closure, which remains an Iteration 088 gate.
