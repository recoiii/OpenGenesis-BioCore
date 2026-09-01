#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text, encoding="utf-8", newline="\n")


def replace_one(text: str, old: str, new: str, label: str) -> str:
    if text.count(old) != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {text.count(old)}")
    return text.replace(old, new, 1)

# 1) Final release identity: default source build is exactly 0.2.0.
path = "CMakeLists.txt"
text = read(path)
text = replace_one(
    text,
    'set(BIOCORE_VERSION_SUFFIX "dev" CACHE STRING "Semantic version suffix; empty for final release")',
    'set(BIOCORE_VERSION_SUFFIX "" CACHE STRING "Semantic version suffix; empty for final release")',
    "final version suffix",
)
write(path, text)

# 2) Release-facing metadata. The v0.2 version-specific Zenodo DOI is intentionally
# not invented before deposition; the stable concept DOI remains the public anchor.
write(
    "CITATION.cff",
    '''cff-version: 1.2.0
message: "If you use OpenGenesis-BioCore in your research, please cite the software using the metadata below."

title: "OpenGenesis-BioCore"
type: software
version: "0.2.0"
date-released: "2026-09-01"
license: MIT

repository-code: "https://github.com/recoiii/OpenGenesis-BioCore"
doi: "10.5281/zenodo.22012037"
url: "https://doi.org/10.5281/zenodo.22012037"

authors:
  - family-names: "Çelik"
    given-names: "Recep"
    orcid: "https://orcid.org/0009-0003-2939-1009"
    affiliation: "Gazi University"

keywords:
  - bioinformatics
  - genomics
  - NGS
  - reproducible workflows
  - workflow management
  - local-first
  - C++
  - variant calling
  - FASTA
  - FASTQ
  - scientific software

abstract: >-
  OpenGenesis-BioCore is a local-first, extensible bioinformatics platform
  designed for reproducible analysis workflows on Windows and Linux. The
  platform combines a C++20 native core with a localhost browser interface,
  SQLite-backed project persistence, managed files, versioned plugins and
  pipelines, immutable execution-plan snapshots, asynchronous job scheduling,
  supervised worker processes, durable recovery, live progress reporting,
  managed-file integrity verification, export/report generation, and output
  artifact tracking.

preferred-citation:
  type: software
  title: "OpenGenesis-BioCore v0.2.0"
  authors:
    - family-names: "Çelik"
      given-names: "Recep"
      orcid: "https://orcid.org/0009-0003-2939-1009"
  version: "0.2.0"
  date-released: "2026-09-01"
  doi: "10.5281/zenodo.22012037"
  url: "https://doi.org/10.5281/zenodo.22012037"
''',
)

write(
    "README.md",
    '''# OpenGenesis-BioCore

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
biocore --init-project ./my-project \\
  --name "My OpenGenesis-BioCore Project" \\
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
''',
)

# 3) Record Iteration 053 acceptance and Iteration 054 scope.
write(
    "docs/development/ITERATION-053-ACCEPTANCE.md",
    '''# Iteration 053 Acceptance

- Verdict: `ACCEPT`
- Confidence: `100%`
- Exact accepted SHA: `d41f09068e3cd90ac41663baa253cf84b9b7cd61`
- CI run: `33506840340`
- Linux matrix: `75/75` GCC Debug, `75/75` GCC Release, `75/75` Clang Debug, `75/75` GCC ASan+UBSan (`300/300`)
- Gemini artifact: `9800014775`
- Artifact digest: `sha256:6f18816849f3ec1a46de63189c84702b1ce18ff5e2752313609c6a49650fb744`
- Blocking findings: none
- Non-blocking findings: none

Iteration 053 is frozen at `accepted/iteration-053`.
''',
)

write(
    "docs/development/ITERATION-054.md",
    '''# Iteration 054 — v0.2.0 Final Release Closure

## Scope

Iteration 054 adds no new scientific feature. It converts the accepted v0.2 development line into one exact
release candidate and binds Linux validation, native Windows validation, installation/package smoke and
independent review to that same source identity.

## Release invariants

- Final executable/version identity is exactly `0.2.0` with no development suffix.
- Project database schema remains v8.
- Worker Protocol remains v2.
- Execution Plan schema remains v4.
- Plugin Protocol remains v2.
- Bundled plugin IDs/versions, 12 pipeline IDs/versions, scientific thresholds and output formats are unchanged.
- Retry continues to reuse the immutable execution plan and exact plugin bindings.
- Localhost/session/path-containment/process-tree/managed-file integrity boundaries remain fail-closed.
- Test floor remains exactly the active 75-test suite; final closure does not delete tests to pass release gates.

## Closure evidence

The final GitHub Actions run must create an exact source archive from the candidate commit, record its SHA-256,
and use that archive as the native Windows build source. Required gates are:

- Linux GCC Debug: 75/75.
- Linux GCC Release: 75/75.
- Linux Clang Debug: 75/75.
- Linux GCC ASan+UBSan: 75/75.
- Exact Linux release identity `0.2.0`.
- Native Windows MSVC Debug: 75/75.
- Native Windows MSVC Release: 75/75.
- Windows clean install, 8 native plugins, 12 pipelines, app-local MSVC runtimes, no System32/SysWOW64 payload.
- Windows `--init-project` smoke.
- CPack `OpenGenesis-BioCore-0.2.0-windows-x64.zip` and extracted-package smoke.
- Source and portable package SHA-256 evidence.
- Exactly four Markdown Gemini review parts generated only after all prerequisite gates pass.

Iteration 054 may be frozen only after independent Gemini returns exact `VERDICT: ACCEPT` for the exact candidate.
''',
)

write(
    "docs/release/RELEASE-NOTES-0.2.0.md",
    '''# OpenGenesis-BioCore v0.2.0 Release Notes

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
''',
)

write(
    "docs/release/FINAL-WINDOWS-CLOSURE.md",
    '''# OpenGenesis-BioCore v0.2.0 — Final Native Windows Closure

The final release is validated against the exact SHA-256-sealed Iteration 054 source archive on a native
Windows x64 host. Historical Windows evidence does not close a newer source candidate.

## Required environment

- Native Windows x64 with an MSVC C++ toolchain supported by the repository Windows presets.
- CMake 3.25 or newer.
- A valid vcpkg checkout containing `vcpkg.exe` and `scripts\\buildsystems\\vcpkg.cmake`.
- `sqlite3:x64-windows`, `drogon:x64-windows` and `zlib:x64-windows` (installed/verified by the script).
- PowerShell with `Expand-Archive` and `Get-FileHash`.

## One-command closure

```powershell
.\\scripts\\validate-windows-final.ps1 `
  -SourceArchive "C:\\path\\to\\OpenGenesis-BioCore-iteration-054-source-CANDIDATE.zip" `
  -ExpectedSourceSha256 "<exact SHA-256>" `
  -VcpkgRoot "C:\\path\\to\\vcpkg"
```

The script verifies the archive hash, extracts that archive into the evidence workspace, and performs every
build/test/package operation from the extracted sealed source. The checkout used to launch the script is not
treated as the build source.

## Native gates

1. MSVC Debug configure/build and exactly 75/75 CTests.
2. MSVC Release configure/build and exactly 75/75 CTests.
3. Clean Release install.
4. Exact installed `0.2.0` identity and healthy Worker Protocol v2 smoke.
5. Exactly eight native plugin manifests/entrypoints.
6. Exactly twelve bundled pipelines.
7. Native plugin no-argument process contract.
8. App-local MSVC runtime DLLs beside Core and each native plugin entrypoint.
9. `--init-project` smoke.
10. CPack `OpenGenesis-BioCore-0.2.0-windows-x64.zip` generation.
11. Extracted-package Core/Worker/plugin/pipeline/frontend smoke.
12. No Windows System32/SysWOW64 payload and no core Windows system DLL bundling.
13. SHA-256 evidence for both exact source archive and portable ZIP.
14. The full regression suite includes the process-tree cancellation/supervisor containment contract.

## Evidence

Successful execution creates `artifacts/windows-final-closure/` plus
`artifacts/OpenGenesis-BioCore-iteration-054-windows-evidence.zip`. The compact evidence bundle contains the
summary, SHA list and top-level command/transcript logs; the portable ZIP remains under the evidence `dist/`
directory and is uploaded separately by the final CI workflow.
''',
)

write(
    "docs/release/VALIDATION.md",
    '''# OpenGenesis-BioCore v0.2.0 Validation Record

## Authoritative release policy

The v0.2.0 release decision is made against one exact Iteration 054 source candidate. Linux source validation,
native Windows validation, installation/package smoke and independent Gemini review must all refer to the same
candidate commit/source archive. Historical v0.1.0 or intermediate v0.2 evidence cannot close a newer source.

## Frozen predecessor

Iteration 053 is accepted and frozen at:

`d41f09068e3cd90ac41663baa253cf84b9b7cd61`

Its Linux matrix passed 75/75 in GCC Debug, GCC Release, Clang Debug and GCC ASan+UBSan (300/300 total), and
Gemini returned `ACCEPT` with 100% confidence and no findings.

## Iteration 054 final gates

The exact release candidate must pass:

- Linux GCC Debug 75/75;
- Linux GCC Release 75/75;
- Linux Clang Debug 75/75;
- Linux GCC ASan+UBSan 75/75;
- exact final `0.2.0` identity;
- browser failure/operational visibility/resource hardening contracts;
- exact pipeline plugin-version pin contract;
- native Windows MSVC Debug 75/75;
- native Windows MSVC Release 75/75;
- clean Windows Release install;
- exactly 8 installed native plugins and 12 installed pipelines;
- app-local MSVC runtime placement beside Core and native plugins;
- no System32/SysWOW64 or core Windows system DLL payload leakage;
- `--init-project` smoke for installed and extracted package trees;
- CPack portable `OpenGenesis-BioCore-0.2.0-windows-x64.zip`;
- SHA-256 identity for exact source archive and Windows portable ZIP;
- exactly four Markdown parts for independent Gemini review after every prerequisite job succeeds.

## Compatibility identity

Finalization changes the human-facing build identity from `0.2.0-dev` to `0.2.0`; it does not change project
schema v8, Worker Protocol v2, Execution Plan schema v4, Plugin Protocol v2, bundled scientific algorithms,
plugin/pipeline IDs, local-only security boundaries or immutable retry semantics.
''',
)

# 4) Final review-package header understands final-release identity and closure prerequisites.
path = "scripts/generate-gemini-review.py"
text = read(path)
old = '''    ci_lines = ""
    if ci_run_id is not None:
        ci_lines = f"""- GitHub Actions validation run: `{ci_run_id}`
- CI prerequisite status: **PASSED before package generation**
- Required prerequisite gates: GCC Debug, GCC Release, Clang Debug, GCC ASan+UBSan; each enforces at least the 67-test v0.1 baseline and a fully passing CTest suite
- Development identity gate: GCC Debug verified `biocore --version` equals `0.2.0-dev`
"""

    return f"""# OpenGenesis-BioCore v0.2.0-dev — Iteration {iteration:03d} Gemini Independent Validation
'''
new = '''    final_release = iteration >= 54
    release_identity = "0.2.0" if final_release else "0.2.0-dev"
    ci_lines = ""
    if ci_run_id is not None:
        if final_release:
            prerequisite_gates = (
                "GCC Debug, GCC Release, Clang Debug, GCC ASan+UBSan and native Windows "
                "MSVC Debug/Release; the final source archive, install/package smoke and SHA-256 evidence "
                "must all pass before package generation"
            )
            identity_gate = "Final release identity gate: `biocore --version` verified exact `0.2.0`"
        else:
            prerequisite_gates = (
                "GCC Debug, GCC Release, Clang Debug, GCC ASan+UBSan; each enforces at least the "
                "67-test v0.1 baseline and a fully passing CTest suite"
            )
            identity_gate = "Development identity gate: GCC Debug verified `biocore --version` equals `0.2.0-dev`"
        ci_lines = f"""- GitHub Actions validation run: `{ci_run_id}`
- CI prerequisite status: **PASSED before package generation**
- Required prerequisite gates: {prerequisite_gates}
- {identity_gate}
"""

    return f"""# OpenGenesis-BioCore v{release_identity} — Iteration {iteration:03d} Gemini Independent Validation
'''
text = replace_one(text, old, new, "Gemini final-release header")
write(path, text)

# 5) Generalize native Windows closure and make it build from the sealed archive.
path = "scripts/validate-windows-final.ps1"
text = read(path)
text = replace_one(
    text,
    '    [string]$EvidenceDirectory = "artifacts/windows-final-closure"\n)',
    '    [string]$EvidenceDirectory = "artifacts/windows-final-closure",\n\n    [string]$ExpectedVersion = "0.2.0",\n\n    [ValidateRange(1, 10000)]\n    [int]$ExpectedCTestCount = 75,\n\n    [ValidateRange(1, 999)]\n    [int]$Iteration = 54\n)',
    "Windows closure parameters",
)
text = text.replace("Assert-True ($version -eq '0.1.0') \"Expected exact installed version 0.1.0, got '$version'\"", "Assert-True ($version -eq $ExpectedVersion) \"Expected exact installed version $ExpectedVersion, got '$version'\"")
text = text.replace("Assert-True ($health.version -eq '0.1.0') \"Installed Core health version is not 0.1.0\"", "Assert-True ($health.version -eq $ExpectedVersion) \"Installed Core health version is not $ExpectedVersion\"")
text = text.replace("Assert-True ((Get-ChildItem -LiteralPath $pipelineRoot -Filter '*.json' -File).Count -ge 11) \"Expected the frozen analysis pipeline suite in the install tree\"", "Assert-True ((Get-ChildItem -LiteralPath $pipelineRoot -Filter '*.json' -File).Count -eq 12) \"Expected exactly 12 frozen analysis pipelines in the install tree\"")
old = '''$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $repoRoot

$archivePath = (Resolve-Path -LiteralPath $SourceArchive).Path
$actualSourceSha = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
$expectedSourceSha = $ExpectedSourceSha256.ToLowerInvariant()
Assert-True ($actualSourceSha -eq $expectedSourceSha) "Source archive SHA-256 mismatch. Expected $expectedSourceSha, got $actualSourceSha"

$script:EvidenceRoot = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $EvidenceDirectory))
if (Test-Path -LiteralPath $script:EvidenceRoot) {
    Remove-Item -LiteralPath $script:EvidenceRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $script:EvidenceRoot -Force | Out-Null
'''
new = '''$invocationRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $invocationRoot

$archivePath = (Resolve-Path -LiteralPath $SourceArchive).Path
$actualSourceSha = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
$expectedSourceSha = $ExpectedSourceSha256.ToLowerInvariant()
Assert-True ($actualSourceSha -eq $expectedSourceSha) "Source archive SHA-256 mismatch. Expected $expectedSourceSha, got $actualSourceSha"

$script:EvidenceRoot = [System.IO.Path]::GetFullPath((Join-Path $invocationRoot $EvidenceDirectory))
if (Test-Path -LiteralPath $script:EvidenceRoot) {
    Remove-Item -LiteralPath $script:EvidenceRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $script:EvidenceRoot -Force | Out-Null

$sourceExtractParent = Join-Path $script:EvidenceRoot 'sealed-source'
New-Item -ItemType Directory -Path $sourceExtractParent -Force | Out-Null
Expand-Archive -LiteralPath $archivePath -DestinationPath $sourceExtractParent -Force
$sourceTopLevelDirectories = @(Get-ChildItem -LiteralPath $sourceExtractParent -Directory)
Assert-True ($sourceTopLevelDirectories.Count -eq 1) "Expected one top-level directory in sealed source archive, found $($sourceTopLevelDirectories.Count)"
$repoRoot = $sourceTopLevelDirectories[0].FullName
Assert-True (Test-Path -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt') -PathType Leaf) 'Sealed source archive does not contain CMakeLists.txt'
Set-Location $repoRoot
'''
text = replace_one(text, old, new, "sealed source extraction")
text = text.replace('Write-Host "OpenGenesis-BioCore v0.1.0 native Windows final closure"', 'Write-Host "OpenGenesis-BioCore v$ExpectedVersion native Windows final closure"')
text = text.replace("Assert-ExactCTestCount 'windows-msvc-debug' 67", "Assert-ExactCTestCount 'windows-msvc-debug' $ExpectedCTestCount")
text = text.replace("Assert-ExactCTestCount 'windows-msvc-release' 67", "Assert-ExactCTestCount 'windows-msvc-release' $ExpectedCTestCount")
text = text.replace("$packages = @(Get-ChildItem -LiteralPath $cpackOutput -Filter 'OpenGenesis-BioCore-0.1.0-windows-x64*.zip' -File)", "$packages = @(Get-ChildItem -LiteralPath $cpackOutput -Filter (\"OpenGenesis-BioCore-{0}-windows-x64*.zip\" -f $ExpectedVersion) -File)")
text = text.replace('Assert-True ($packages.Count -eq 1) "Expected exactly one OpenGenesis-BioCore-0.1.0-windows-x64 ZIP, found $($packages.Count)"', 'Assert-True ($packages.Count -eq 1) "Expected exactly one OpenGenesis-BioCore-$ExpectedVersion-windows-x64 ZIP, found $($packages.Count)"')
text = text.replace("        release = '0.1.0'", "        release = $ExpectedVersion")
text = text.replace("        debugCTestCount = 67", "        debugCTestCount = $ExpectedCTestCount")
text = text.replace("        releaseCTestCount = 67", "        releaseCTestCount = $ExpectedCTestCount")
text = text.replace("$bundlePath = Join-Path $bundleParent 'OpenGenesis-BioCore-iteration-044-windows-evidence.zip'", "$bundlePath = Join-Path $bundleParent (\"OpenGenesis-BioCore-iteration-{0:D3}-windows-evidence.zip\" -f $Iteration)")
write(path, text)

# 6) Final CI: exact source archive, four Linux configs, native Windows closure, then Gemini package.
write(
    ".github/workflows/v0.2-validation.yml",
    '''name: OpenGenesis-BioCore v0.2 final validation

on:
  push:
    branches:
      - v0.2.0-dev
  pull_request:
    branches:
      - main
      - v0.2.0-dev
  workflow_dispatch:

env:
  BIOCORE_ITERATION: "054"
  BIOCORE_TEST_FLOOR: "75"
  BIOCORE_RELEASE_VERSION: "0.2.0"

permissions:
  contents: read

jobs:
  source-candidate:
    name: Exact source candidate
    runs-on: ubuntu-24.04
    steps:
      - name: Checkout exact candidate
        uses: actions/checkout@v5
        with:
          fetch-depth: 0

      - name: Create SHA-256 sealed source archive
        shell: bash
        run: |
          mkdir -p artifacts/source
          archive="artifacts/source/OpenGenesis-BioCore-iteration-${BIOCORE_ITERATION}-source-CANDIDATE.zip"
          git archive --format=zip --prefix="OpenGenesis-BioCore-${BIOCORE_RELEASE_VERSION}/" --output="${archive}" HEAD
          sha256sum "${archive}" | tee artifacts/source/SHA256SUMS.txt
          git rev-parse HEAD | tee artifacts/source/COMMIT.txt

      - name: Upload exact source candidate
        uses: actions/upload-artifact@v4
        with:
          name: OpenGenesis-BioCore-iteration-${{ env.BIOCORE_ITERATION }}-source-CANDIDATE
          path: artifacts/source/
          if-no-files-found: error
          retention-days: 30

  linux-matrix:
    name: ${{ matrix.preset }}
    runs-on: ubuntu-24.04
    strategy:
      fail-fast: false
      matrix:
        preset:
          - linux-gcc-debug
          - linux-gcc-release
          - linux-clang-debug
    steps:
      - name: Checkout exact candidate
        uses: actions/checkout@v5
        with:
          fetch-depth: 0

      - name: Install build dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y ninja-build libsqlite3-dev zlib1g-dev clang

      - name: Configure
        run: cmake --preset "${{ matrix.preset }}"

      - name: Build
        run: cmake --build --preset "${{ matrix.preset }}" --parallel

      - name: Require exact active test suite
        shell: bash
        run: |
          count="$(ctest --preset "${{ matrix.preset }}" -N | sed -n 's/^Total Tests: //p')"
          test "${count}" = "${BIOCORE_TEST_FLOOR}"
          echo "CTest count: ${count}"

      - name: Run regression suite
        run: ctest --preset "${{ matrix.preset }}" --output-on-failure

      - name: Verify exact v0.2.0 release identity
        if: matrix.preset == 'linux-gcc-debug'
        shell: bash
        run: |
          actual="$(build/linux-gcc-debug/apps/biocore/biocore --version)"
          test "${actual}" = "${BIOCORE_RELEASE_VERSION}"
          grep -F 'set(BIOCORE_VERSION_SUFFIX "" CACHE STRING' CMakeLists.txt
          echo "Final release identity: ${actual}"

      - name: Verify browser failure evidence contract
        if: matrix.preset == 'linux-gcc-debug'
        shell: bash
        run: |
          node --check frontend/assets/app.js
          node - <<'JS'
          const { core } = require('./frontend/assets/app.js');
          const normalized = core.normalizeJob({
            id: 'failure-ui', status: 'failed', failure: {
              kind: 'worker_reported_failure', message: 'worker failed', exitCode: 17,
              workerTimestampUtc: '2026-09-01T08:00:00Z', recordedAtUtc: '2026-09-01T08:00:01Z'
            }
          });
          if (!normalized || normalized.failure?.exitCode !== 17 || normalized.failure?.message !== 'worker failed') process.exit(1);
          const state = { jobs: new Map([['failure-ui', { ...normalized, status: 'running', failure: null }]]), logs: new Map(), cursors: new Map() };
          const result = core.applyLifecycle(state, {
            type: 'worker.lifecycle', eventType: 'failed', jobId: 'failure-ui', launchRevision: 1,
            sequence: 1, workerTimestampUtc: '2026-09-01T08:00:02Z', message: 'terminal failure', exitCode: 9
          });
          if (!result.accepted || state.jobs.get('failure-ui').failure?.kind !== 'worker_reported_failure' || state.jobs.get('failure-ui').failure?.exitCode !== 9) process.exit(2);
          console.log('Browser failure evidence contract PASS');
          JS

      - name: Verify frontend operational visibility contract
        if: matrix.preset == 'linux-gcc-debug'
        shell: bash
        run: |
          node --check frontend/assets/app.js
          node - <<'JS'
          const { core } = require('./frontend/assets/app.js');
          const job = core.normalizeJob({
            id: 'job-visibility', status: 'interrupted', attemptNumber: 3, revision: 9, progress: 0.4,
            failure: { kind: 'heartbeat_timeout', message: 'worker heartbeat expired', exitCode: null, workerTimestampUtc: null, recordedAtUtc: '2026-09-01T10:00:00Z' }
          });
          if (!job || job.attemptNumber !== 3 || !core.canRetryJob(job.status) || core.canRetryJob('failed') || !core.canVerifyExport('completed') || core.canVerifyExport('running')) process.exit(1);
          const digest = 'a'.repeat(64);
          const manifest = core.normalizeExportManifest({
            schemaVersion: 1, producer: { name: 'OpenGenesis-BioCore', version: '0.2.0' }, stableSnapshot: true,
            artifactCount: 1, report: { jobId: 'job-visibility', attemptNumber: 3 },
            artifacts: [{ metadata: { managedFileId: 'artifact-1' }, verifiedSha256: digest }]
          });
          if (!manifest || manifest.attemptNumber !== 3 || manifest.artifacts.length !== 1) process.exit(2);
          if (core.artifactVerificationState({ managedFileId: 'artifact-1', checksumAlgorithm: 'sha256', checksumValue: digest }, manifest) !== 'verified') process.exit(3);
          if (core.artifactVerificationState({ managedFileId: 'artifact-1', checksumAlgorithm: 'sha256', checksumValue: 'b'.repeat(64) }, manifest) !== 'mismatch') process.exit(4);
          console.log('Frontend operational visibility behavior PASS');
          JS

      - name: Verify resource and concurrency hardening contract
        if: matrix.preset == 'linux-gcc-debug'
        shell: bash
        run: |
          set +e
          invalid_output="$(build/linux-gcc-debug/apps/biocore/biocore --serve . --max-concurrent-jobs 65 2>&1)"
          invalid_status=$?
          set -e
          test "${invalid_status}" -eq 1
          grep -F "Maximum concurrent jobs must be an integer between 1 and 64" <<<"${invalid_output}"
          grep -F "default_maximum_concurrent_jobs = 2U" src/application/include/biocore/application/job_scheduler.hpp
          grep -F "maximum_supported_concurrent_jobs = 64U" src/application/include/biocore/application/job_scheduler.hpp
          grep -F "using SharedMessage = std::shared_ptr<const std::string>" src/presentation/include/biocore/presentation/worker_lifecycle_event_broadcast.hpp

      - name: Verify exact pipeline plugin-version pins
        if: matrix.preset == 'linux-gcc-debug'
        shell: bash
        run: |
          python3 - <<'PY'
          import json
          from pathlib import Path
          module_versions = {}
          for manifest_path in Path('plugins').glob('*/plugin.json'):
              manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
              for module in manifest['modules']:
                  module_versions[module['id']] = manifest['version']
          pipelines = sorted(Path('pipelines').glob('*.json'))
          if len(pipelines) != 12:
              raise SystemExit(f'Expected 12 bundled pipelines; found {len(pipelines)}')
          for path in pipelines:
              document = json.loads(path.read_text(encoding='utf-8'))
              if document.get('schemaVersion') != 2:
                  raise SystemExit(f'{path}: expected pipeline schema v2')
              for step in document['steps']:
                  expected = module_versions.get(step['module'])
                  if expected is None or step.get('pluginVersion') != expected:
                      raise SystemExit(f"{path}: plugin pin mismatch for {step['module']}")
          print('Pipeline plugin-version pin contract PASS')
          PY

  sanitizers:
    name: linux-gcc-debug-asan-ubsan
    runs-on: ubuntu-24.04
    steps:
      - name: Checkout exact candidate
        uses: actions/checkout@v5
        with:
          fetch-depth: 0
      - name: Install build dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y ninja-build libsqlite3-dev zlib1g-dev
      - name: Configure ASan + UBSan
        run: |
          cmake -S . -B build/linux-gcc-asan-ubsan -G Ninja \\
            -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=g++ \\
            -DBIOCORE_BUILD_TESTS=ON -DBIOCORE_WARNINGS_AS_ERRORS=ON \\
            -DBIOCORE_ENABLE_ASAN=ON -DBIOCORE_ENABLE_UBSAN=ON \\
            -DBIOCORE_VERSION_SUFFIX=
      - name: Build ASan + UBSan
        run: cmake --build build/linux-gcc-asan-ubsan --parallel
      - name: Require exact active test suite
        shell: bash
        run: |
          count="$(ctest --test-dir build/linux-gcc-asan-ubsan -N | sed -n 's/^Total Tests: //p')"
          test "${count}" = "${BIOCORE_TEST_FLOOR}"
      - name: Run ASan + UBSan regression suite
        env:
          ASAN_OPTIONS: detect_leaks=1:halt_on_error=1
          UBSAN_OPTIONS: halt_on_error=1:print_stacktrace=1
        run: ctest --test-dir build/linux-gcc-asan-ubsan --output-on-failure

  windows-native-closure:
    name: Native Windows final closure
    needs:
      - source-candidate
    runs-on: windows-latest
    steps:
      - name: Checkout closure launcher
        uses: actions/checkout@v5
        with:
          fetch-depth: 0
      - name: Download exact source candidate
        uses: actions/download-artifact@v5
        with:
          name: OpenGenesis-BioCore-iteration-${{ env.BIOCORE_ITERATION }}-source-CANDIDATE
          path: artifacts/source
      - name: Resolve hosted vcpkg root
        shell: pwsh
        run: |
          if (-not $env:VCPKG_ROOT -and $env:VCPKG_INSTALLATION_ROOT) {
            "VCPKG_ROOT=$env:VCPKG_INSTALLATION_ROOT" | Out-File -FilePath $env:GITHUB_ENV -Encoding utf8 -Append
          }
      - name: Validate exact sealed source on native Windows
        shell: pwsh
        run: |
          $archive = Get-ChildItem artifacts/source/OpenGenesis-BioCore-iteration-054-source-CANDIDATE.zip | Select-Object -First 1
          $line = Get-Content artifacts/source/SHA256SUMS.txt | Select-Object -First 1
          $sha = ($line -split '\\s+')[0]
          .\\scripts\\validate-windows-final.ps1 -SourceArchive $archive.FullName -ExpectedSourceSha256 $sha -ExpectedVersion "0.2.0" -ExpectedCTestCount 75 -Iteration 54
      - name: Upload Windows closure evidence
        uses: actions/upload-artifact@v4
        with:
          name: OpenGenesis-BioCore-iteration-${{ env.BIOCORE_ITERATION }}-windows-final-closure
          path: |
            artifacts/windows-final-closure/
            artifacts/OpenGenesis-BioCore-iteration-054-windows-evidence.zip
          if-no-files-found: error
          retention-days: 30

  gemini-review-package:
    name: Gemini four-part review package
    needs:
      - source-candidate
      - linux-matrix
      - sanitizers
      - windows-native-closure
    runs-on: ubuntu-24.04
    steps:
      - name: Checkout exact validated candidate
        uses: actions/checkout@v5
        with:
          fetch-depth: 0
      - name: Generate exact four-part Markdown package
        run: |
          python3 scripts/generate-gemini-review.py \\
            --iteration "${BIOCORE_ITERATION}" \\
            --parts 4 \\
            --ci-run-id "${{ github.run_id }}" \\
            --output artifacts/gemini-review
      - name: Verify package shape and checksums
        shell: bash
        run: |
          mapfile -t parts < <(find artifacts/gemini-review -maxdepth 1 -type f -name '*-part-*-of-04.md' -print | sort)
          test "${#parts[@]}" -eq 4
          cd artifacts/gemini-review
          sha256sum -c "OpenGenesis-BioCore-iteration-${BIOCORE_ITERATION}-GEMINI-review-SHA256SUMS.txt"
      - name: Upload Gemini review package
        uses: actions/upload-artifact@v4
        with:
          name: OpenGenesis-BioCore-iteration-${{ env.BIOCORE_ITERATION }}-GEMINI-review
          path: artifacts/gemini-review/
          if-no-files-found: error
          retention-days: 30
''',
)

print("Iteration 054 final-closure transformation complete")
