# OpenGenesis-BioCore v0.2.0-dev — Iteration 050

## Title

Plugin & Pipeline Contract Hardening

## Goal

Make pipeline-to-plugin resolution exact and reproducible by eliminating silent plugin-version drift and carrying the resolved plugin contract into immutable execution-plan snapshots.

Project database schema remains v8. Worker Protocol remains v2. Pipeline Definition schema advances to v2 and Execution Plan schema advances to v4.

## Intended changes

- require canonical namespaced pipeline identifiers and semantic pipeline versions;
- require canonical namespaced module identifiers and exact semantic plugin-version pins on every pipeline step;
- advance bundled pipeline documents to schema v2 with `pluginVersion` on every step;
- reject planning when the discovered module version differs from the pipeline pin;
- expose plugin manifest version and API version through the registry contract;
- snapshot plugin manifest/API identity into execution-plan schema v4;
- revalidate plugin namespace, semantic versions, supported manifest range, and API version when reconstructing an execution plan;
- preserve existing plugin entrypoint canonicalization, no-symlink rules, platform selection, I/O contracts, and out-of-process execution;
- add a dedicated hardening CTest and raise the Linux matrix floor from 71 to 72;
- add a CI guard proving all 12 bundled pipelines pin the exact version declared by their shipped plugin manifest.

## Explicit non-goals

Iteration 050 must not change project DB schema v8, Worker Protocol v2, job retry/failure semantics, managed-file integrity behavior, biological algorithms, scientific thresholds, loopback security boundaries, or process-tree supervision. It does not introduce plugin dependency solving, version ranges, remote registries, or dynamic plugin installation.

## Acceptance criteria

1. Pipeline Definition schema v2 requires an exact semantic `pluginVersion` per step.
2. All 12 bundled pipelines use schema v2 and match their shipped plugin manifest version exactly.
3. Pipeline ids/module ids use canonical namespaced identifiers and pipeline/plugin versions use semantic versioning.
4. Planning rejects unavailable modules, plugin-version mismatches, unsupported manifest versions, unsupported API versions, or namespace mismatches.
5. Execution Plan schema v4 records plugin id/version, manifest version, API version, module type, plugin root, and executable path.
6. Execution-plan parsing/reconstruction rejects malformed or unsupported plugin contract metadata.
7. Existing parameter/input/output binding contracts and immutable execution-plan snapshot semantics are preserved.
8. Active CTest floor is at least 72 and GCC Debug, GCC Release, Clang Debug, and GCC ASan+UBSan all pass.
9. Development identity remains exactly `0.2.0-dev`.
10. Gemini review package is exactly four Markdown parts and Iteration 050 remains open until independent `ACCEPT`.
