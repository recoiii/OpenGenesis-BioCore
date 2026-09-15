# OpenGenesis-BioCore v0.3.0 — Final Native Windows Closure

The final release is validated against the exact SHA-256-sealed Iteration 068 source archive on a native Windows
x64 host. Historical Windows evidence does not close a newer source candidate.

## Required environment

- Native Windows x64 with an MSVC C++ toolchain supported by the repository Windows presets.
- CMake 3.25 or newer.
- A valid vcpkg checkout containing `vcpkg.exe` and `scripts\buildsystems\vcpkg.cmake`.
- `sqlite3:x64-windows`, `drogon:x64-windows` and `zlib:x64-windows` (installed/verified by the script).
- PowerShell with `Expand-Archive` and `Get-FileHash`.

## One-command closure

```powershell
.\scripts\validate-windows-final.ps1 `
  -SourceArchive "C:\path\to\OpenGenesis-BioCore-iteration-068-source-CANDIDATE.zip" `
  -ExpectedSourceSha256 "<exact SHA-256>" `
  -ExpectedVersion "0.3.0" `
  -ExpectedCTestCount 125 `
  -Iteration 68 `
  -VcpkgRoot "C:\path\to\vcpkg"
```

The explicit v0.3 arguments are authoritative; the validation script is shared with earlier releases. It verifies
the archive hash, extracts the archive into its evidence workspace and performs build/test/package operations from
the extracted sealed source rather than from the launcher checkout.

## Native gates

1. MSVC Debug configure/build and exactly 125/125 CTests.
2. MSVC Release configure/build and exactly 125/125 CTests.
3. Clean Release install.
4. Exact installed `0.3.0` identity and healthy Worker Protocol v2 smoke.
5. Exactly eight native plugin manifests/entrypoints.
6. Exactly twelve bundled pipelines.
7. Native plugin no-argument process contract.
8. App-local MSVC runtime DLLs beside Core and each native plugin entrypoint.
9. `--init-project` smoke.
10. CPack `OpenGenesis-BioCore-0.3.0-windows-x64.zip` generation.
11. Extracted-package Core/Worker/plugin/pipeline/frontend smoke.
12. No Windows System32/SysWOW64 payload and no core Windows system DLL bundling.
13. SHA-256 evidence for both exact source archive and portable ZIP.
14. The full regression suite retains process-tree cancellation/supervisor containment coverage.

## Evidence

Successful execution creates `artifacts/windows-final-closure/` plus
`artifacts/OpenGenesis-BioCore-iteration-068-windows-evidence.zip`. The portable package remains under the evidence
`dist/` directory. CI uploads the closure evidence generated from the same sealed source archive used by all native
Windows operations.
