# Project Workspace v1

## Scope

Iteration 004 added the first project workspace initializer. Iteration 005 extends the committed `.biocore` area with `project.sqlite`; see [Project Database v1](project-database-v1.md). The workspace still does not create projects at arbitrary new paths, open existing OpenGenesis-BioCore workspaces, or expose the workflow through HTTP.

## Application contract

`IProjectWorkspace::initialize(Project)` returns an `IProjectWorkspaceTransaction`.

- Initialization must complete before `IProjectRepository::save` is called.
- Destroying an uncommitted transaction rolls back only artifacts created by that initialization.
- `commit()` is `noexcept` and is called only after repository persistence succeeds.
- A failed repository save therefore cannot leave OpenGenesis-BioCore-created workspace artifacts behind.
- Repository `save` implementations are required to be atomic: a failed save must not leave a partially persisted project.

## Filesystem adapter policy

`FilesystemProjectWorkspace` accepts only an existing canonical directory. It rejects:

- missing roots,
- filesystem volume/root directories,
- non-canonical paths containing unresolved aliases,
- roots whose final path entry is a symbolic link,
- any root already containing a reserved OpenGenesis-BioCore top-level entry.

Unrelated files and directories are allowed and are never removed.

## Reserved workspace tree

```text
<project-root>/
├── .biocore/
│   ├── ownership.json
│   ├── project.sqlite
│   ├── locks/
│   ├── runtime/
│   └── cache/
├── inputs/
├── work/
│   ├── jobs/
│   └── temporary/
├── outputs/
├── reports/
└── logs/
```

The reserved top-level names are `.biocore`, `inputs`, `work`, `outputs`, `reports`, and `logs`. If any one already exists as a file, directory, or symbolic link, initialization fails before persistent project registration.

## Ownership metadata

`.biocore/ownership.json` is written through a temporary file and renamed into place. Version 1 contains:

```json
{
  "schemaVersion": 1,
  "projectId": "<project-id>",
  "createdAtUtc": "<UTC timestamp>"
}
```

JSON control characters are escaped. No external resource, network access, or executable content is included.

## Rollback safety

Rollback tracks exact paths created by the current initialization and removes them in reverse order with non-recursive deletion.

This is intentional. If another process or the user adds a file after initialization but before rollback, OpenGenesis-BioCore leaves that untracked content in place rather than recursively deleting it. Empty OpenGenesis-BioCore-created siblings are still removed where safe.

## Known boundaries

- Process-crash recovery for an orphaned, uncommitted workspace is not implemented yet.
- Opening and validating an already committed workspace is not implemented yet.
- Windows MSVC and Windows filesystem case behavior remain unverified in the current environment.
- Filesystem operations cannot eliminate all time-of-check/time-of-use races without platform-specific directory-handle APIs; the current implementation minimizes the surface with canonical roots, reserved-name preflight, exact-path rollback, and atomic metadata rename.
