# Managed File Registration v1

Iteration 007 introduces managed input-file registration without parsing biological content.

## Boundary

The Domain layer owns `ManagedFile` and `StorageMode`. The Application layer owns
`IManagedFileRepository`, `IInputFileStorage`, the import transaction contract, and
`ManagedFileService`. Filesystem and SQLite details remain in Infrastructure.

## Registration order

1. Validate the source-path and file-type request before side effects.
2. Read one UTC timestamp for the logical registration attempt.
3. Generate an identifier and reject known identifier collisions before copying.
4. Create a transactionally managed copy under `inputs/<file-id>/<display-name>`.
5. Construct the `ManagedFile` aggregate from storage-produced metadata.
6. Insert the aggregate into the project-local repository.
7. Commit the filesystem transaction only after persistence succeeds.

An exception or uniqueness conflict before step 7 destroys the uncommitted import
transaction. Rollback removes only the exact final or temporary file and the ID directory
when empty; unrelated files are not recursively deleted.

## Filesystem safety rules

- Project root and `inputs` must already exist and be canonical non-symlink directories.
- The managed-file identifier must be a single ASCII alphanumeric, hyphen, or underscore path segment.
- The source must be an existing non-symlink regular file.
- The destination directory is unique per managed-file identifier.
- Copying writes to a tracked temporary path in the destination directory.
- Source size and modification timestamp are checked before and after copying.
- Copied size must equal the source size.
- Publication uses a same-directory rename.
- Same display names do not collide because each file has its own identifier directory.

These checks reduce but do not eliminate platform-level TOCTOU races. Directory-handle-based
platform adapters remain a future hardening item.

## Persistence

`SqliteManagedFileRepository` uses prepared statements and the project-local
`managed_files` table created in schema v1. It supports insertion, lookup by ID, lookup by
relative project path, and deterministic listing. Both primary-key and unique relative-path
conflicts return `false`; other SQLite errors remain exceptions.

## Explicit exclusions

- FASTA/FASTQ/BAM/VCF content inspection
- Checksum computation
- Copy progress and cancellation
- External-reference and managed-move use cases
- File deletion, archive, or reconciliation
- Opening and repairing existing managed-file artifacts
