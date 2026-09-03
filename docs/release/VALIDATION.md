# OpenGenesis-BioCore v0.2.0 Validation Record

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
