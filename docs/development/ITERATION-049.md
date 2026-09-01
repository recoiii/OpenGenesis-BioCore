# OpenGenesis-BioCore v0.2.0-dev — Iteration 049

## Title

Managed-File Integrity & Large-File I/O

## Goal

Make newly imported managed inputs content-addressable by durable SHA-256 evidence, keep large-file work bounded in memory, and provide a fail-closed integrity inspection path that detects deletion, size changes, same-size content tampering, unsafe path redirection, and files changing during verification.

Project database schema remains v8 because checksum metadata columns already exist. Worker Protocol remains v2.

## Intended changes

- replace managed input `copy_file` with an explicit fixed-buffer streaming copy that computes SHA-256 while bytes are copied;
- keep the file hashing read buffer fixed at 64 KiB and expose that bound as a testable infrastructure constant;
- calculate SHA-256 for finalized browser uploads before publishing them into managed inputs;
- persist `checksum_algorithm=sha256` and the lowercase digest on every managed copy created by the application after this iteration;
- preserve pre-existing checksum-less records as readable legacy data rather than inventing or backfilling unverifiable digests;
- add filesystem integrity inspection that validates project-relative path containment, non-symlink regular-file identity, byte size, SHA-256, and file stability while hashing;
- expose managed-file checksum evidence in existing file JSON and add `GET /api/v1/files/{id}/integrity`;
- add a large-file/tamper integration contract and raise the active CTest floor from 70 to 71.

## Explicit non-goals

Iteration 049 must not:

- change biological algorithms, thresholds, formats, or scientific outputs;
- change project database schema version 8;
- rewrite historical checksum-less managed-file records;
- automatically mutate, repair, or delete a file that fails integrity verification;
- change Worker Protocol v2, plugin manifests, pipeline definitions, or immutable execution-plan snapshots;
- add remote/object/cloud storage;
- change localhost/browser security boundaries or process-tree cancellation ownership;
- make frontend operational-visibility changes reserved for Iteration 052.

## Acceptance criteria

1. Managed copy import uses a fixed-size streaming buffer and does not read the whole source into process memory.
2. The streamed copy records a lowercase SHA-256 digest matching the copied byte sequence.
3. Browser upload finalization records a SHA-256 digest and detects staging mutation during hashing.
4. `ManagedFileService` persists checksum algorithm/value for newly created managed copies.
5. Legacy managed-copy records without checksum evidence remain readable and report `checksum_unavailable`.
6. Integrity inspection returns `verified` for an unchanged managed input.
7. Same-size byte tampering returns `checksum_mismatch`.
8. Truncation/size changes return `size_mismatch` before digest comparison.
9. Missing files return `file_missing`; symlink/path escape or metadata path redirection returns `unsafe_path`.
10. A file that changes while verification is hashing it returns `changed_during_verification`.
11. File JSON exposes persisted checksum evidence and `GET /api/v1/files/{id}/integrity` exposes only bounded integrity metadata, not file contents.
12. Active CTest floor is at least 71 and GCC Debug, GCC Release, Clang Debug, and GCC ASan+UBSan all pass.
13. The exact candidate is packaged in the standard four-part Gemini review format and remains open until independent `ACCEPT`.

## Freeze rule

Do not create `accepted/iteration-049` until Gemini returns exact `VERDICT: ACCEPT`.
