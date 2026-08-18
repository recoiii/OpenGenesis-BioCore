# OpenGenesis-BioCore

OpenGenesis-BioCore is a local-first C++20 bioinformatics platform with a localhost browser UI, native worker
processes, SQLite project persistence, plugin/pipeline execution, live job telemetry, durable artifacts,
and an integrated FASTA/FASTQ-to-variant-analysis workflow suite.

**Current release: v0.1.0**

## Highlights

- Project workspace initialization through `biocore --init-project`.
- Loopback-only local web server and browser frontend.
- Bootstrap bearer -> independent HttpOnly SameSite=Strict browser session handoff.
- Bounded browser managed-file import using sequential 1 MiB chunks, an 8 GiB file cap, rollback-safe
  staging, and a 30-minute inactivity TTL.
- Exact pipeline/version job preparation with immutable execution-plan snapshots.
- Native out-of-process workers and plugins with shell-free process launch and process-tree cancellation ownership.
- Live WebSocket job lifecycle telemetry and bounded in-memory browser logs.
- Durable generated artifacts with provenance and SHA-256 integrity verification.
- JSON/HTML execution reports and verified artifact download preparation.
- FASTA DNA QC plus single/paired FASTQ QC and trimming.
- Native ungapped alignment, SAM/BAM QC, coverage/depth and deterministic SNV calling.
- User-selectable VCF QC/filtering and local TSV-backed variant annotation with offline HTML/JSON/TSV reports.
- Analysis Wizard, grouped Results navigation and explicit job cancellation.
- Conservative startup recovery, bounded worker control-plane retention and partial-output quarantine.

## Validation

Final v0.1.0 acceptance is tied to one exact source candidate and its reproducible Linux + native Windows
closure evidence. Historical Windows evidence is not reused for later source states. The final Windows
script validates MSVC Debug/Release, all eight native plugins, app-local runtimes, clean install, CPack ZIP
and extracted-package smoke against the exact source SHA-256.

See [`docs/release/VALIDATION.md`](docs/release/VALIDATION.md) and
[`docs/release/FINAL-WINDOWS-CLOSURE.md`](docs/release/FINAL-WINDOWS-CLOSURE.md).

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
2. Open **Analysis Wizard** and choose one of the frozen workflows.
3. Select explicit managed inputs and parameters.
4. Review the exact pipeline/version and bindings JSON.
5. Submit the prepared job.
6. Follow live progress, cancel if needed, and open **Results** for reports and artifacts.

## Security posture

OpenGenesis-BioCore binds the supported web surface to `127.0.0.1`, sends no telemetry, and does not place bearer
secrets in URLs or browser storage. Native plugins are trusted local executables and are **not sandboxed**;
installing a plugin is equivalent to installing native software from that publisher. See
[`SECURITY.md`](SECURITY.md).

## License

OpenGenesis-BioCore is distributed under the [MIT License](LICENSE).
