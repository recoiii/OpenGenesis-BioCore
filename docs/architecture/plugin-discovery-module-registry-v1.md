# OpenGenesis-BioCore Plugin Discovery and Module Registry v1

**Iteration:** 013  
**Status:** Candidate for independent review  
**Scope:** Plugin identity, manifest validation, filesystem discovery, module resolution, and
shell-free deterministic process execution. Real bioinformatics modules and typed data contracts are
out of scope.

## 1. Architectural boundaries

### Domain

The Domain layer owns:

- `PluginPlatform`
- `PluginModuleType`
- `PluginEntrypoint`
- `PluginModuleDefinition`
- `PluginManifest`

It validates identifiers, namespace ownership, semantic versions, API compatibility, safe relative
entrypoint syntax, unique module IDs, and unique platform entrypoints. It has no JSON, filesystem,
process, SQLite, or Application dependency.

### Plugin Protocol

`plugin_protocol` owns the bounded wire representation of `plugin.json`. Schema v1 is strict:
unknown and duplicate fields are rejected, UTF-8 and Unicode escapes are validated, and document,
nesting, text, module, and entrypoint counts are bounded.

### Application

The Application layer owns:

- `IPluginRegistry`
- `RegisteredPlugin`
- `ResolvedPluginModule`
- `IPluginProcessRunner`

`PipelinePlanner` resolves every pipeline module through `IPluginRegistry`. The resulting
`ExecutionPlan` contains immutable plugin identity and executable metadata. Application code does not
scan directories, parse plugin JSON, or call operating-system process APIs.

### Infrastructure

`FilesystemPluginRegistry` discovers plugins and implements atomic registry refresh.
`PlatformPluginProcessRunner` revalidates manifest-to-executable binding at execution time and launches
the process without a shell.

## 2. Plugin manifest schema v1

```json
{
  "manifestVersion": 1,
  "id": "org.biocore.demo",
  "name": "OpenGenesis-BioCore Demo Plugin",
  "version": "0.1.0",
  "apiVersion": "1.0",
  "publisher": "OpenGenesis-BioCore Project",
  "modules": [
    {
      "id": "org.biocore.demo.validate",
      "type": "process",
      "entrypoints": {
        "linux-x64": "bin/linux-x64/biocore-demo-plugin",
        "windows-x64": "bin/windows-x64/biocore-demo-plugin.exe"
      }
    }
  ]
}
```

Rules include:

- plugin and module IDs are lower-case dotted identifiers;
- every module ID is inside the plugin namespace;
- plugin versions use Semantic Versioning, including valid prerelease/build identifiers;
- API version is exactly `1.0`;
- entrypoints are relative, forward-slash-separated, and contain no empty, `.`, `..`, backslash,
  drive-prefix, NUL, or control-character segment;
- only process modules are supported in v1.

## 3. Discovery algorithm

1. Require each configured plugin root to be an existing canonical non-symlink directory that is not
   a filesystem root.
2. Enumerate immediate non-symlink child directories in deterministic native-path order.
3. Require canonical plugin-local `plugin.json`.
4. Parse strict Plugin Manifest schema v1 and construct Domain objects.
5. Resolve the active platform entrypoint.
6. Require a canonical non-symlink regular file beneath the plugin directory; POSIX entrypoints must
   be executable.
7. Collect candidates without modifying the live registry.
8. Count plugin IDs and module IDs across all valid candidates.
9. Reject every candidate participating in a duplicate plugin or module conflict.
10. Sort accepted plugins/modules and atomically replace the registry under a lock.

Invalid candidates produce typed discovery issues and do not stop unrelated valid plugins from
loading.

## 4. Execution-plan binding

Execution-plan schema v2 stores, per step:

- plugin ID and version;
- module ID and module type;
- canonical plugin root;
- canonical executable path.

The worker does not contain a hardcoded module allow-list. Before every plugin launch the process
runner:

1. revalidates canonical root and executable containment;
2. reopens the plugin-local manifest;
3. checks plugin ID, plugin version, and API version;
4. resolves the exact module and active-platform entrypoint;
5. canonicalizes the manifest entrypoint and requires it to equal the execution-plan executable;
6. launches the executable with a native argv/command-line representation and no shell.

This second binding check narrows the trust gap between discovery/plan preparation and execution. It
does not fully eliminate platform-level TOCTOU replacement races.

## 5. Demo process modules

The installed demo manifest declares deterministic `validate`, `scan`, `report`, and `fail` modules.
The first three exit successfully; `fail` exits with code 3. Plugin stdout is isolated from worker
lifecycle stdout. The worker converts a non-zero plugin exit into a correctly sequenced lifecycle
`failed` event and native worker exit code 3.

## 6. Deferred boundaries

- plugin signatures and publisher trust;
- enable/disable state and persistent registry metadata;
- quarantine and sandboxing;
- typed parameters, inputs, outputs, and artifacts;
- plugin stdout result protocol;
- resource limits and process-tree containment;
- native Windows/MSVC runtime validation;
- complete TOCTOU elimination and explicit durability flush;
- real bioinformatics tools.
