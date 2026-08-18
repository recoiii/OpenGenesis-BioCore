# Contributing to OpenGenesis-BioCore

OpenGenesis-BioCore uses small, reviewable, validated changes.

Before submitting a change:

1. Keep Core logic local-first and deterministic where practical.
2. Do not introduce shell-based worker/plugin process launch.
3. Preserve the localhost-only security boundary unless a future design explicitly changes it.
4. Add or update tests for behavioral changes.
5. Run the relevant CMake/CTest preset before submitting.

By contributing, you agree that your contribution may be distributed under the project's MIT License.
