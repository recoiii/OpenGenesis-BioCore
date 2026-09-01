# OpenGenesis-BioCore v0.2.0-dev — Iteration 045

## Title

Development Baseline, Compatibility Guardrails & Repeatable Gemini Review Packaging

## Goal

Open the v0.2 development line from the frozen v0.1.0 technical baseline without changing biological algorithms, persistence semantics, plugin/pipeline identifiers, Worker Protocol v2, project schema v6 or the localhost-only security boundary.

Iteration 045 also establishes repeatable CI validation and deterministic four-part Markdown review packaging so later v0.2 iterations can use the same independent Gemini acceptance workflow.

## Intended changes

- change Core project version from `0.1.0` to `0.2.0`;
- make the default development identity `0.2.0-dev` through `BIOCORE_VERSION_SUFFIX=dev`;
- keep all normal development presets, including Windows Release, on the `0.2.0-dev` identity;
- reserve an empty version suffix for an explicit final-release closure command/configuration rather than a normal development preset;
- retain v0.1.0 citation/DOI metadata as historical release metadata;
- add the v0.2 roadmap and explicit compatibility boundary;
- add automated Linux regression validation for GCC Debug, GCC Release, Clang Debug and GCC ASan+UBSan;
- verify the built CLI reports the expected development version and guard against a development preset silently forcing a final-release suffix;
- generate four UTF-8 Markdown source-review parts with per-file SHA-256 metadata, CI-run provenance and a package checksum manifest.

## Explicit non-goals

Iteration 045 must not:

- change project or catalog database schemas;
- introduce a migration;
- alter Worker Protocol v2;
- change any biological calculation;
- bump plugin or pipeline versions merely because Core enters v0.2 development;
- rename compatibility identifiers such as `org.biocore.*`, `.biocore`, `X-BioCore-*` or established VCF machine identifiers;
- expand the network/security boundary beyond loopback-only local operation;
- create or publish a final `v0.2.0` release artifact.

## Acceptance criteria

1. The v0.2 branch builds with warnings-as-errors under GCC Debug, GCC Release and Clang Debug.
2. GCC Debug with AddressSanitizer + UndefinedBehaviorSanitizer passes.
3. Every existing CTest passes in every executed Linux configuration and the 67-test v0.1 baseline is not reduced.
4. `biocore --version` from a normal development configuration reports exactly `0.2.0-dev`.
5. No normal development configure preset forces `BIOCORE_VERSION_SUFFIX` to an empty final-release value.
6. No project schema, Worker Protocol, plugin/pipeline identifier or biological algorithm change is present in the Iteration 045 delta.
7. The source-review generator produces exactly four Markdown parts from the exact checked-out commit.
8. Each represented UTF-8 file block records source-relative path, byte length and SHA-256.
9. Non-UTF-8 files, if any, are represented by metadata rather than silently omitted.
10. Review-package checksums are generated deterministically for the produced Markdown parts, and the package records the prerequisite CI run id.
11. Independent Gemini review returns `ACCEPT` before the iteration is frozen.

## Gemini verdict contract

The required primary verdict is exactly one of:

- `ACCEPT`
- `REJECT`

The reviewer may report non-blocking findings under an `ACCEPT` verdict, but any blocking defect requires `REJECT`.

Required response structure:

```text
VERDICT: ACCEPT | REJECT
CONFIDENCE: <0-100>%

BLOCKING FINDINGS:
- NONE

NON-BLOCKING FINDINGS:
- ...

INVARIANT STATUS:
- v0.1 schema v6 compatibility: PRESERVED | NOT PRESERVED
- Worker Protocol v2: PRESERVED | NOT PRESERVED
- plugin/pipeline compatibility identifiers: PRESERVED | NOT PRESERVED
- biological behavior: PRESERVED | NOT PRESERVED
- localhost security boundary: PRESERVED | NOT PRESERVED
- development version identity: PRESERVED | NOT PRESERVED

RATIONALE:
...
```

## Freeze rule

Iteration 045 remains open until CI evidence is complete and Gemini returns `ACCEPT`. Only then may the exact candidate SHA be recorded as the frozen baseline for Iteration 046.
