# OpenGenesis-BioCore v0.5.0 — Final Native Windows Closure

The final release is validated against the exact SHA-256-sealed Iteration 088 source archive on native Windows x64.
Historical Windows evidence does not close a newer source candidate.

## Required environment

- Native Windows x64 with supported MSVC toolchain.
- CMake 3.25 or newer.
- vcpkg with `sqlite3:x64-windows`, `drogon:x64-windows` and `zlib:x64-windows`.
- PowerShell with `Expand-Archive` and `Get-FileHash`.

## One-command closure

```powershell
.\scripts\validate-windows-final.ps1 `
  -SourceArchive "C:\path\to\OpenGenesis-BioCore-iteration-088-source-CANDIDATE.zip" `
  -ExpectedSourceSha256 "<exact SHA-256>" `
  -ExpectedVersion "0.5.0" `
  -ExpectedCTestCount 288 `
  -ExpectedWorkflowTemplateCount 1 `
  -Iteration 88 `
  -VcpkgRoot "C:\path\to\vcpkg"
```

The script verifies the archive hash, extracts the sealed source and performs all builds/tests/package operations
from that extracted source, not from an unsealed launcher checkout.

## Native gates

1. MSVC Debug configure/build and exactly 288/288 CTests.
2. MSVC Release configure/build and exactly 288/288 CTests.
3. Clean Release install and exact installed `0.5.0` identity.
4. Healthy Worker Protocol v2 smoke.
5. Exactly eight native plugins, twelve analysis pipelines and one workflow template.
6. Native plugin no-argument process contract and app-local MSVC runtime placement.
7. `--init-project` smoke from installed tree.
8. CPack `OpenGenesis-BioCore-0.5.0-windows-x64.zip` generation.
9. Extracted-package Core/Worker/plugin/pipeline/template/frontend and project-init smoke.
10. No Windows System32/SysWOW64 payload and no core Windows system DLL bundling.
11. SHA-256 evidence for exact source archive and portable ZIP.
12. Full suite includes accepted v0.5 migration/recovery/project-workspace E2E regression coverage.

Successful execution creates `artifacts/windows-final-closure/` and
`artifacts/OpenGenesis-BioCore-iteration-088-windows-evidence.zip`.
