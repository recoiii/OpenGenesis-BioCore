# OpenGenesis-BioCore v0.1.0 Release Notes

OpenGenesis-BioCore v0.1.0 is a local-first native bioinformatics platform with a localhost browser UI, durable
project execution, reproducible pipeline snapshots and out-of-process native plugins.

## Platform

- C++20 Core and isolated native Worker runtime.
- SQLite project/catalog persistence, recovery and immutable execution-plan snapshots.
- Authenticated loopback-only REST/WebSocket browser surface.
- Managed-file import, durable artifacts, SHA-256 provenance and report/download preparation.
- Long-running worker heartbeats, bounded noisy-worker control-plane retention and process-tree-safe user job cancellation.
- Browser Analysis Wizard covering the frozen analysis suite and grouped Results navigation.

## Analysis suite

- FASTA DNA QC and statistics.
- Single and paired FASTQ QC, including gzip FASTQ support.
- Single and paired adapter/quality trimming.
- Native deterministic ungapped reference alignment.
- SAM/BAM alignment QC, coverage/depth and extended alignment statistics.
- Deterministic SNV-only variant calling with VCFv4.3 output.
- User-selectable VCF DP/AC/AF/ABQ QC/filter policy with audit-preserving soft filtering.
- Local TSV-backed variant annotation and offline VCF/JSON/TSV/HTML results reporting.

## Distribution

- MIT License.
- Portable Windows x64 CPack ZIP.
- App-local MSVC runtime placement beside Core and all eight native plugin executable directories.
- Windows system-directory dependencies are excluded from the portable payload.
- Final consolidated source/package acceptance is tied to exact SHA-256 identities and reproducible
  Linux + native Windows closure evidence.

See `VALIDATION.md` and `FINAL-WINDOWS-CLOSURE.md` for the release gates and evidence procedure.
