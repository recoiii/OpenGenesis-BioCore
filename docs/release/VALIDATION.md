# OpenGenesis-BioCore v0.1.0 Validation Record

## Current final-closure policy

The authoritative v0.1.0 release decision is made against one exact final source candidate. Linux source
validation and native Windows packaging validation must refer to that same source identity. Native Windows
closure is reproducible through `scripts/validate-windows-final.ps1`; see `FINAL-WINDOWS-CLOSURE.md`. The
script resolves an explicit vcpkg toolchain (`-VcpkgRoot`, `VCPKG_ROOT`, toolchain environment, or PATH),
verifies/installs the three frozen Windows dependency triplets, and treats native exit code—not stderr
warning text—as the command success boundary.

The release must not reuse an older package hash as proof for a newer source candidate.

## Historical native Windows baseline — 2026-08-08

An earlier v0.1.0 source state was validated natively with Visual Studio 2026 Professional / MSVC
19.51.36252:

- MSVC Debug: 67/67 CTests passed;
- MSVC Release: 67/67 CTests passed;
- targeted Win32 process/plugin/worker contracts passed;
- `/W4 /permissive- /EHsc /utf-8 /WX` policy passed;
- clean install and `--init-project` smoke passed;
- exact `0.1.0` identity passed;
- app-local MSVC runtime checks passed;
- CPack ZIP and extracted-package smoke passed.

Historical package:

`BioCore-0.1.0-windows-x64.zip`

Historical SHA-256:

`ee88069b2408622fe08a20048b7342e1ae407a1e67517f51d40d52ecc19c9995`

This hash is retained only as historical evidence. It does **not** validate source changes introduced
after that closure.

## Historical Iteration 042 final closure — superseded

The Iteration 042 R4 source completed the prior Linux and native Windows closure, but an independent
source audit subsequently identified incomplete process-tree cancellation. Its exact source SHA-256
`39af68f84654d1a844c51d3b2c07e9f304beb3cf929bbd6fe79904de785a3e03` and its Windows package hash
are retained as historical evidence only and no longer identify the publishable v0.1.0 source.

## Historical Iteration 043 engineering closure — superseded by branding consolidation

Iteration 043 closed the process-tree cancellation and malformed-input findings and passed Linux/native Windows engineering validation. Its exact source SHA-256 is `4eaa3862f9aea644839d95c32c3d12fb959e8d537f9e69731f6f3586158105f3`; its Windows portable package SHA-256 is `46d2dc04596138c8cb6681c7d7ec3e5668cc23f0f480ae08225bf74cbc67abbb`. These are now historical engineering evidence because Iteration 044 changes public branding and package identity.

## Current Iteration 044 gates

For the exact Iteration 044 candidate, acceptance requires:

- Linux GCC Debug 67/67;
- Linux GCC Release 67/67;
- Linux Clang Debug 67/67;
- Linux GCC ASan+UBSan 67/67 with sanitizer linkage;
- process-tree cancellation regression PASS on POSIX and native Windows;
- the old single-PID cancellation behavior is killed by the regression contract;
- malformed FASTA/FASTQ/gzip/SAM/BAM/VCF negative-input regression fixtures PASS;
- native Windows MSVC Debug 67/67;
- native Windows MSVC Release 67/67;
- all eight current native plugins present and loadable from the installed and extracted package;
- app-local MSVC runtime placement beside Core and every native plugin executable;
- no Windows System32/SysWOW64 payload leakage;
- clean Release install and exact `0.1.0` identity;
- `--init-project` smoke;
- portable CPack ZIP and extracted-package smoke;
- SHA-256 identity for both exact source candidate and final portable ZIP.

Iteration 044 preserves the Iteration 043 execution/runtime behavior while consolidating the public product identity to `OpenGenesis-BioCore`. Internal compatibility identifiers (`biocore` executables, `org.biocore.*`, `BioCore::`, `BIOCORE_*`, `.biocore`, `X-BioCore-*`, and established VCF identifiers) remain unchanged.

The final acceptance evidence is generated outside the immutable source candidate so the source hash does
not change after native validation.

- Windows PowerShell 5.1 plugin no-argument contract probes are executed through the native capture wrapper; stderr usage text is not treated as a script failure, and the required native exit code remains exactly 2.

### R3 deterministic Visual Studio build regeneration policy

The final native-Windows closure configures both MSVC build trees with `CMAKE_SUPPRESS_REGENERATION=ON`. The source archive is already SHA-256 sealed before native validation, so the build is not permitted to mutate its CMake inputs. Suppressing the Visual Studio regeneration target prevents parallel MSBuild from launching an automatic configure/CPack regeneration pass while compilation is in progress. Explicit Debug and Release configure steps still run before each build.

### R4 PowerShell empty-argument probe correction

The final Windows closure native-command wrapper explicitly accepts an empty argument collection.
This is required for the intentional no-argument native plugin contract probes, where each plugin is
expected to print usage and exit with code 2. Windows PowerShell parameter binding must not reject
`@()` before the native executable is launched.
