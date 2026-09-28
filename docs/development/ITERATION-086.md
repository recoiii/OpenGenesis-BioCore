# Iteration 086 — Results Packaging, HTML Report & JSON Manifest

Status: **CANDIDATE DEVELOPMENT**. No candidate is accepted/frozen until independent Gemini review
returns `VERDICT: ACCEPT` for the exact commit and exact source SHA-256.

Frozen baseline:

- ref: `accepted/iteration-085`
- commit: `5997d5f7f00ba23c3b5833f43c0e8dd7c101794f`

## Objective

Export the lineage-aware Iteration 085 batch result snapshot as an integrity-verified,
provenance-preserving JSON manifest and human-readable HTML report without copying/recomputing
scientific outputs or introducing a new analysis method.

## Deliverables

- `BatchResultPackage` schema v1;
- `BatchResultPackageService` export-time persistence identity and physical-integrity verification;
- deterministic JSON manifest renderer;
- escaped, self-contained HTML report renderer;
- localhost authenticated batch export routes;
- package-level stable/complete snapshot flags;
- explicit persisted-vs-verified SHA-256 evidence for each current artifact;
- tests for verified export, disappearing artifact, identity drift, checksum/integrity failure,
  JSON determinism/no-path-leak and HTML escaping/no-path-leak;
- retained Iteration 079–085 and v0.4 regression coverage.

## Acceptance gates

1. JSON and HTML are generated from the same `BatchResultPackage` snapshot and do not independently
   recalculate scientific results.
2. Every current artifact is re-resolved from persistence and exact identity-checked against the
   Iteration 085 lineage-aware snapshot immediately before export.
3. Every exported artifact entry passes fresh size/SHA-256/filesystem-safety verification; missing,
   mutated, unsafe, non-regular or unverifiable content aborts export with no partial success.
4. Only project-relative paths are serialized; absolute managed/content paths never appear in JSON
   or HTML.
5. Missing QC values remain explicit null/absence; package/report code does not impute zero or create
   a derived metric.
6. Manifest/report preserve sample, latest Job/attempt, module/plugin, QC comparison, issue and
   artifact provenance needed to interpret the result snapshot.
7. Renderer ordering is deterministic for a fixed package; verified artifact entries have a stable
   total ordering independent of repository enumeration order.
8. HTML escapes untrusted data, contains no script/remote asset, and is served with no-store,
   nosniff and restrictive CSP headers.
9. Export surfaces are authenticated under the existing localhost API boundary and fail closed when
   the package service or underlying verified snapshot is unavailable.
10. Iteration 086 introduces no project-schema migration, Job mutation, new scientific scoring,
    VCF parser/matrix implementation, or cross-sample execution fan-in.
11. The exact source candidate must pass the full four-lane v0.5 CI and independent Gemini review
    before `accepted/iteration-086` may be created.

## Out of scope

- archival container generation (ZIP/TAR);
- digital signatures/PKI/notarization;
- native Windows release closure;
- Iteration 087/088 UI/release work not required to prove the package contract.

## Contribution

Project owner/developer: **Recep Çelik**.

AI-assisted architecture, implementation, validation, debugging, evidence collation, and review
package preparation: **ChatGPT**.
