# OpenGenesis-BioCore

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.22012037.svg)](https://doi.org/10.5281/zenodo.22012037)

OpenGenesis-BioCore is a local-first C++20 bioinformatics platform with a localhost browser UI, native worker
processes, SQLite project persistence, plugin/pipeline execution, live job telemetry, durable artifacts,
and an integrated FASTA/FASTQ-to-variant-analysis workflow suite.

**Current stable release: v0.2.0**

## Highlights

- Project workspace initialization through `biocore --init-project`.
- Loopback-only local web server and browser frontend.
- Bootstrap bearer -> independent HttpOnly SameSite=Strict browser session handoff.
- Bounded browser managed-file import using sequential 1 MiB chunks, an 8 GiB file cap, rollback-safe staging,
  a 30-minute inactivity TTL, and streaming SHA-256 integrity verification.
- Exact pipeline/version job preparation with immutable execution-plan snapshots and fail-closed plugin bindings.
- Native out-of-process workers and plugins with shell-free process launch and process-tree cancellation ownership.
- Explicit worker concurrency budget: default 2, hard maximum 64.
- Live WebSocket job lifecycle telemetry with shared immutable payloads and bounded subscriber queues.
- Structured durable failure diagnostics, conservative startup recovery, and explicit interrupted-job retry semantics.
- Durable generated artifacts with provenance and SHA-256 integrity verification.
- JSON/HTML execution reports, verified artifact download preparation, and portable export manifests.
- FASTA DNA QC plus single/paired FASTQ QC and trimming.
- Native ungapped alignment, SAM/BAM QC, coverage/depth and deterministic SNV calling.
- User-selectable VCF QC/filtering and local TSV-backed variant annotation with offline HTML/JSON/TSV reports.
- Analysis Wizard, grouped Results navigation, retry visibility, integrity state, and explicit job cancellation.

## Validation

OpenGenesis-BioCore v0.2.0 is closed against one exact source candidate. Linux GCC Debug, GCC Release,
Clang Debug and GCC ASan+UBSan validation, plus native Windows MSVC Debug/Release, clean install,
portable CPack ZIP and extracted-package smoke, must all refer to that same source archive identity.
Historical Windows evidence is never reused for a newer source state.

See [`docs/release/VALIDATION.md`](docs/release/VALIDATION.md),
[`docs/release/FINAL-WINDOWS-CLOSURE.md`](docs/release/FINAL-WINDOWS-CLOSURE.md), and
[`docs/release/RELEASE-NOTES-0.2.0.md`](docs/release/RELEASE-NOTES-0.2.0.md).

## Build on Linux

```bash
cmake --preset linux-gcc-debug
cmake --build --preset linux-gcc-debug --parallel
ctest --preset linux-gcc-debug
```

## Build on Windows

Use an x64 Visual Studio Developer PowerShell with CMake and the required dependencies available through
your toolchain/vcpkg setup:

```powershell
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release --parallel
ctest --preset windows-msvc-release
```

## Create a project

The project root can be a new directory whose parent already exists. Keep the global catalog outside the
project workspace.

```bash
biocore --init-project ./my-project \
  --name "My OpenGenesis-BioCore Project" \
  --catalog ./biocore-data/catalog.sqlite
```

Then start the local server:

```bash
biocore --serve ./my-project
```

The terminal prints the localhost UI URL and a one-time process bootstrap bearer token. Paste that token
into the browser session form; the browser session uses a separate HttpOnly cookie.

## First analysis

1. Open the browser UI and import the required managed files.
2. Open **Analysis Wizard** and choose a bundled workflow.
3. Select explicit managed inputs and parameters.
4. Review the exact pipeline/version and bindings JSON.
5. Submit the prepared job.
6. Follow live progress, retry interrupted work when appropriate, and open **Results** for reports and artifacts.

## Security posture

OpenGenesis-BioCore binds the supported web surface to `127.0.0.1`, sends no telemetry, and does not place bearer
secrets in URLs or browser storage. Native plugins are trusted local executables and are **not sandboxed**;
installing a plugin is equivalent to installing native software from that publisher. See
[`SECURITY.md`](SECURITY.md).

## License

OpenGenesis-BioCore is distributed under the [MIT License](LICENSE).

## Citation

The stable Zenodo concept DOI for OpenGenesis-BioCore is **10.5281/zenodo.22012037**. The repository citation
metadata identify this source as v0.2.0. A version-specific v0.2.0 Zenodo DOI can be added in a metadata-only
post-release update after the archive is deposited; it is intentionally not fabricated in the frozen source.

Citation metadata are available in [`CITATION.cff`](CITATION.cff).

## Software availability

- Source code: https://github.com/recoiii/OpenGenesis-BioCore
- DOI for all archived versions: https://doi.org/10.5281/zenodo.22012037
- Previous archived release v0.1.0: https://doi.org/10.5281/zenodo.22012038
- License: MIT
