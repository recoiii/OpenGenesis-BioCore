# OpenGenesis-BioCore v0.2.0-dev — Iteration 051

## Export & Report Foundation

Iteration 051 establishes a portable, integrity-verified export contract without introducing an archive-format dependency or changing scientific output.

### Changes

- pipeline execution report JSON advances to schema v2 and records durable `attemptNumber`;
- HTML reports expose the execution attempt;
- a new export manifest schema v1 records producer version, stable-snapshot status, the complete report, artifact metadata, and a freshly verified SHA-256 for every generated artifact;
- export manifest generation reuses the existing artifact integrity verifier and fails closed on missing, unsafe, size-mismatched, checksum-less, or checksum-mismatched content;
- absolute local filesystem paths are intentionally excluded from JSON report/export surfaces;
- `GET /api/v1/jobs/{id}/export-manifest.json` exposes the portable manifest through the existing authenticated local API;
- artifact ordering remains deterministic by step, output port, and managed-file identity;
- a dedicated integration test raises the active test floor from 72 to 73.

### Compatibility boundaries

- project database schema remains v8;
- Worker Protocol remains v2;
- Pipeline Definition remains schema v2;
- Execution Plan remains schema v4;
- no biological algorithm, threshold, output format, scheduler, retry, security, process-supervision, or managed-file storage semantics are changed;
- no ZIP/TAR writer is introduced in this iteration. The manifest is the format-neutral foundation for later packaging/export UI work.

### Acceptance criteria

1. Report schema v2 contains attempt identity and remains safe for HTML/JSON rendering.
2. Export manifest schema v1 contains producer identity, stable-snapshot status, report, deterministic artifact order, and verified SHA-256 digests.
3. Export construction fails closed when any artifact fails integrity verification.
4. Export/report JSON never exposes internal absolute content paths.
5. The authenticated local API exposes `export-manifest.json`.
6. All four Linux validation configurations pass at least 73 CTests with no sanitizer findings.
