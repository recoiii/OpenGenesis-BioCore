# OpenGenesis-BioCore v0.4.0 — Final Native Windows Closure

The final release is validated against the exact SHA-256-sealed Iteration 078 source archive on a native Windows
x64 host. Historical Windows evidence does not close a newer source candidate.

## Required environment

- Native Windows x64 with an MSVC C++ toolchain supported by the repository Windows presets.
- CMake 3.25 or newer.
- A valid vcpkg checkout containing `vcpkg.exe` and `scripts\buildsystems\vcpkg.cmake`.
- `sqlite3:x64-windows`, `drogon:x64-windows` and `zlib:x64-windows`.
- PowerShell with `Expand-Archive` and `Get-FileHash`.

## One-command closure

```powershell
.\scripts\validate-windows-final.ps1 `
  -SourceArchive "C:\path\to\OpenGenesis-BioCore-iteration-078-source-CANDIDATE.zip" `
  -ExpectedSourceSha256 "<exact SHA-256>" `
  -ExpectedVersion "0.4.0" `
  -ExpectedCTestCount 224 `
  -ExpectedWorkflowTemplateCount 1 `
  -Iteration 78 `
  -VcpkgRoot "C:\path\to\vcpkg"
```

The validation script verifies the archive hash, extracts the sealed source into the evidence workspace and performs
all build/test/package operations from that extracted source rather than from the launcher checkout.

## Native gates

1. MSVC Debug configure/build and exactly 224/224 CTests.
2. MSVC Release configure/build and exactly 224/224 CTests.
3. Clean Release install.
4. Exact installed `0.4.0` identity and healthy Worker Protocol v2 smoke.
5. Exactly eight native plugin manifests/entrypoints.
6. Exactly twelve installed analysis pipelines matched by `*.biocore-pipeline.json`.
7. Exactly one installed reusable workflow template matched by `*.workflow-template.json`.
8. Native plugin no-argument process contract.
9. App-local MSVC runtime DLLs beside Core and each native plugin entrypoint.
10. `--init-project` smoke.
11. CPack `OpenGenesis-BioCore-0.4.0-windows-x64.zip` generation.
12. Extracted-package Core/Worker/plugin/pipeline/template/frontend smoke.
13. No Windows System32/SysWOW64 payload and no core Windows system DLL bundling.
14. SHA-256 evidence for both exact source archive and portable ZIP.
15. Full regression coverage includes v0.3 schema/assets compatibility and Workflow Engine 2.0 E2E recovery.

## Evidence

Successful execution creates `artifacts/windows-final-closure/` plus
`artifacts/OpenGenesis-BioCore-iteration-078-windows-evidence.zip`. The portable package remains under the evidence
`dist/` directory. CI uploads the closure evidence generated from the same sealed source archive used by all native
Windows operations.
