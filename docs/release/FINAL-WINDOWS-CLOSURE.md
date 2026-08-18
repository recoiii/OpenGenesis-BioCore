# OpenGenesis-BioCore v0.1.0 — Final Native Windows Closure

The final consolidated release must be validated against the **exact frozen source candidate** on a
native Windows x64 host. Historical Windows evidence from earlier BioCore/OpenGenesis-BioCore source states is useful as a
baseline but does not close the current release candidate.

## Required environment

Use an x64 Visual Studio Developer PowerShell with:

- Visual Studio 2026 / MSVC x64 C++ toolchain;
- CMake 3.25 or newer;
- a valid vcpkg checkout containing `vcpkg.exe` and `scripts\buildsystems\vcpkg.cmake`;
- the OpenGenesis-BioCore Windows development dependencies (`sqlite3:x64-windows`, `drogon:x64-windows`,
  `zlib:x64-windows`), which the closure script verifies/installs through that existing vcpkg checkout;
- PowerShell 7 or Windows PowerShell 5.1 with `Expand-Archive` and `Get-FileHash`.

## One-command closure

Keep the downloaded source candidate ZIP available, extract it, open PowerShell in the extracted source
root, and run:

```powershell
.\scripts\validate-windows-final.ps1 `
  -SourceArchive "C:\path\to\OpenGenesis-BioCore-iteration-044-source-CANDIDATE.zip" `
  -ExpectedSourceSha256 "<exact SHA-256 supplied with the candidate>" `
  -VcpkgRoot "C:\path\to\vcpkg"
```

The script fails closed if the source archive hash does not match the expected candidate. `-VcpkgRoot` may
be omitted when `VCPKG_ROOT`, a vcpkg `CMAKE_TOOLCHAIN_FILE`, or `vcpkg.exe` on `PATH` resolves the same
checkout. The script passes the resolved toolchain explicitly to both Windows configure presets and removes
stale Windows build directories before configuring.

Windows PowerShell 5.1 can surface native-process stderr as PowerShell error records even when the native
process exits successfully. The closure wrapper therefore records merged stdout/stderr but decides native
command success strictly from the native exit code; benign CMake developer warnings cannot masquerade as
build failures.

## Native gates

The script performs all of the following on the same source state:

1. configure/build/test MSVC Debug;
2. require exactly 67 Debug CTests and 67/67 PASS;
3. configure/build/test MSVC Release;
4. require exactly 67 Release CTests and 67/67 PASS;
5. clean Release install;
6. exact installed `0.1.0` identity and healthy Worker Protocol v2 smoke;
7. all eight native plugin manifests and Windows entrypoints;
8. no-argument native plugin process contract;
9. app-local MSVC runtime DLL presence beside Core and every native plugin entrypoint;
10. `--init-project` smoke;
11. CPack portable `OpenGenesis-BioCore-0.1.0-windows-x64.zip` generation;
12. extracted-package Core/Worker/plugin/pipeline/frontend smoke;
13. absence of bundled Windows system DLL payloads and System32/SysWOW64 paths;
14. SHA-256 generation for the exact source candidate and produced portable ZIP;
15. the cross-platform `infrastructure.platform_worker_supervisor` regression proving worker cancellation terminates a live descendant process tree.

## Evidence

Successful execution creates `artifacts/windows-final-closure/` containing:

- command logs;
- a full PowerShell transcript;
- installed and extracted-package smoke trees;
- `windows-final-closure-summary.json`;
- `SHA256SUMS.txt`;
- `dist/OpenGenesis-BioCore-0.1.0-windows-x64.zip`.

On success the script also creates `artifacts/OpenGenesis-BioCore-iteration-044-windows-evidence.zip`, a compact
evidence bundle containing the summary, SHA list and top-level command/transcript logs (the portable
package itself remains under `dist/`). Upload that evidence ZIP back to the OpenGenesis-BioCore review workflow.

A release closure review should receive the summary, SHA list, relevant logs and exact package hash. The
source candidate is not accepted merely because an older Windows package passed.

## Packaging invariant

Every native plugin is an independent Windows process. Therefore the portable package installs the MSVC
redistributable runtime beside each plugin executable, not only beside `biocore.exe`. Non-system runtime
dependencies discovered by CMake are installed through each plugin's runtime-dependency set. Windows
System32/SysWOW64 dependencies remain excluded from the portable payload.

- Windows PowerShell 5.1 plugin no-argument contract probes are executed through the native capture wrapper; stderr usage text is not treated as a script failure, and the required native exit code remains exactly 2.

### R3 deterministic Visual Studio build regeneration policy

The final native-Windows closure configures both MSVC build trees with `CMAKE_SUPPRESS_REGENERATION=ON`. The source archive is already SHA-256 sealed before native validation, so the build is not permitted to mutate its CMake inputs. Suppressing the Visual Studio regeneration target prevents parallel MSBuild from launching an automatic configure/CPack regeneration pass while compilation is in progress. Explicit Debug and Release configure steps still run before each build.

### R4 PowerShell empty-argument probe correction

The final Windows closure native-command wrapper explicitly accepts an empty argument collection.
This is required for the intentional no-argument native plugin contract probes, where each plugin is
expected to print usage and exit with code 2. Windows PowerShell parameter binding must not reject
`@()` before the native executable is launched.
