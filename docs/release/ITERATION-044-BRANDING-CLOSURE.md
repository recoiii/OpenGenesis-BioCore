# OpenGenesis-BioCore v0.1.0 — Iteration 044 Branding Consolidation

Iteration 044 changes the public product identity from **BioCore** to **OpenGenesis-BioCore** without changing the v0.1.0 execution model, biological algorithms, persistence schema, worker protocol, plugin IDs, pipeline IDs, or on-disk workspace format.

## Public identity updated

- README, security/contribution/release documentation;
- browser UI titles, labels and user guidance;
- CLI/runtime diagnostics and generated HTML report titles;
- plugin display names and publisher;
- CMake project display identity;
- Windows CPack package/vendor/file name;
- Windows closure/evidence artifact names;
- MIT copyright attribution.

## Compatibility identifiers intentionally retained

The following identifiers remain unchanged because they are protocol, API, build, filesystem or data-format contracts rather than product branding:

- executables: `biocore`, `biocore-worker`;
- C++ namespace/include paths and CMake aliases such as `biocore::` and `BioCore::`;
- build/configuration macros such as `BIOCORE_*`;
- plugin and pipeline identifiers under `org.biocore.*`;
- reserved workspace/install paths such as `.biocore` and `share/biocore`;
- HTTP compatibility headers `X-BioCore-*`;
- established VCF identifiers such as `BioCoreVCFQC`, `BioCoreVariantAnnotation`, `BioCoreUnfiltered`, and `BioCoreLow*`.

Changing these identifiers would be a compatibility migration rather than a branding-only release closure and is intentionally outside Iteration 044.

## Validation policy

Because branding changes touch source files and Windows CPack identity, Iteration 044 must be validated as a new exact source candidate. Linux source/test gates and native Windows Debug/Release/install/CPack/extracted-package gates must refer to the new Iteration 044 SHA-256.
