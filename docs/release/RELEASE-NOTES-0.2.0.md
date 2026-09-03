# OpenGenesis-BioCore v0.2.0 Release Notes

OpenGenesis-BioCore v0.2.0 is the platform-maturation release following v0.1.0. It focuses on production
readiness and operational evidence rather than expanding the biological algorithm suite.

## Major changes since v0.1.0

- Project database compatibility/integrity guard upgraded through schema v8.
- Structured durable job-failure evidence and browser-visible failure diagnostics.
- Explicit interrupted-job retry semantics with immutable execution-plan reuse and attempt tracking.
- Managed-file SHA-256 integrity verification and bounded 64 KiB large-file streaming I/O.
- Hardened plugin/pipeline typed-I/O and exact version-binding contracts.
- Export/report foundation with stable manifests and verified artifact hashes.
- Browser operational visibility for attempts, failures, integrity and export verification state.
- Scheduler concurrency budget: default 2, hard maximum 64.
- WebSocket lifecycle payload sharing removes per-subscriber JSON duplication while preserving queue limits.

## Compatibility

- Project database schema: v8, with migration/integrity guards for earlier supported project states.
- Worker Protocol: v2.
- Execution Plan schema: v4.
- Plugin Protocol: v2.
- Existing `org.biocore.*`, `.biocore`, `share/biocore`, `X-BioCore-*`, `BioCore::*` and established VCF machine identifiers remain compatibility boundaries.
- Bundled analysis suite remains 8 native plugins and 12 pipelines.

## Validation policy

The release is accepted only when Linux GCC Debug/Release, Clang Debug, GCC ASan+UBSan and native Windows
MSVC Debug/Release all pass the full 75-test suite on the exact release candidate. Windows packaging validation
also covers clean install, app-local runtimes, plugin process smoke, project initialization, CPack ZIP extraction,
and system-DLL exclusion.

## Citation

The stable concept DOI is `10.5281/zenodo.22012037`. A version-specific v0.2.0 DOI is intentionally added only
after the accepted release source is deposited to Zenodo.
