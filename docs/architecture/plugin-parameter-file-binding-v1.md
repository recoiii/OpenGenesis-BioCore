# Plugin Parameter and File Binding Architecture v1

Status: Iteration 014 candidate  
Date: 2026-08-07

## 1. Purpose

Iteration 014 defines the first typed data contract between a OpenGenesis-BioCore execution plan and an
out-of-process plugin. It does not add a real bioinformatics analysis. It establishes how validated
parameters and project-owned file bindings become an immutable, per-step invocation snapshot that a
plugin may consume without receiving arbitrary user-supplied command-line arguments.

## 2. Dependency boundaries

- Domain owns parameter types, value invariants, input ports, and output ports.
- Application resolves user bindings against plugin contracts and ManagedFile metadata.
- Pipeline Protocol serializes resolved execution-plan bindings, currently schema version 3.
- Plugin Protocol serializes plugin manifests, currently versions 1-2, and invocation snapshots,
  currently schema version 1.
- Infrastructure owns project path resolution, immutable invocation snapshots, plugin discovery,
  manifest rebinding, and native process launch.
- Plugins receive only the invocation snapshot path through `--invocation`.

No Domain or Application component parses JSON or performs filesystem/process operations.

## 3. Typed parameter contract

Supported parameter types are:

- `string`
- `integer`
- `number`
- `boolean`
- `enum`

A parameter definition can be required or optional and can provide a typed default. Numeric
parameters can declare inclusive minimum/maximum values. Enumeration parameters declare the allowed
string values. Unknown parameters, duplicate bindings, type mismatches, non-finite numeric values,
out-of-range values, and missing required values are rejected before an execution plan is stored.

Numeric text conversion uses locale-independent `std::from_chars` parsing.

## 4. File port contract

A plugin module can declare:

- input ports with a required flag and one or more accepted OpenGenesis-BioCore file-type labels;
- output ports with a single declared file-type label.

An input binding can resolve from:

1. a project `ManagedFile`; or
2. an output of a direct upstream pipeline dependency.

ManagedFile bindings use project-relative paths already owned by OpenGenesis-BioCore. A downstream step can bind
only to a declared output port of a direct dependency and the output file type must satisfy the input
port contract.

## 5. Output ownership

Plugins do not choose arbitrary output paths. OpenGenesis-BioCore produces a deterministic flat namespace:

`outputs/<job-id>--<step-id>--<port-name>.out`

The invocation store rejects output destinations outside the project `outputs/` directory and rejects
nested output paths. Existing output files are never overwritten by invocation preparation.

## 6. Execution-plan schema v3

Each resolved execution step contains:

- plugin identity and version;
- module type, root, and executable;
- parameter/input/output definitions;
- resolved typed parameter values;
- resolved input source kind, source identity, file type, and project-relative path;
- OpenGenesis-BioCore-generated output file type and project-relative destination.

The execution plan remains a project-internal immutable snapshot. It does not contain arbitrary plugin
CLI fragments.

## 7. Invocation snapshot

Immediately before a step is executed the worker creates:

`.biocore/runtime/jobs/<job-id>/invocation-<step-id>-r<revision>.json`

The snapshot contains the job/revision/step/module identity plus resolved parameters and absolute
canonical input/output paths. Managed inputs must be existing canonical regular files inside the
project. Upstream outputs must already exist under the project outputs tree. New outputs must use the
flat OpenGenesis-BioCore outputs namespace and must not already exist.

Snapshot publication uses a temporary file followed by rename. Existing snapshots are immutable and
are not overwritten.

## 8. Process boundary

`PlatformPluginProcessRunner` revalidates the plugin manifest/executable binding and requires the
invocation snapshot to be an absolute, canonical, non-symlink regular file before launch. Native
process arguments are shell-free:

`<plugin-executable> --module-id <module-id> --step-id <step-id> --invocation <snapshot-path>`

The plugin receives no free-form user command-line parameters.

## 9. Compatibility

Plugin Manifest v1 remains readable for modules without I/O contracts. Manifest v1 is forbidden from
containing v2 parameter/input/output contracts, both at the Domain boundary and during serialization,
so contract data cannot be silently discarded. Manifest v2 carries the typed contracts.

## 10. Demonstration module

`org.biocore.demo.copy` proves the contract end-to-end. It consumes typed parameters, a required text
input, and a OpenGenesis-BioCore-owned output destination. The integration fixture binds a ManagedFile, runs the
real worker and plugin processes, and validates the produced output bytes. It is deterministic test
infrastructure, not a bioinformatics analysis.

## 11. Explicit boundaries

Iteration 014 does not yet provide plugin signing/trust, sandboxing, resource limits, plugin stdout
artifact/result protocols, output artifact registration in SQLite, checksums, full filesystem TOCTOU
elimination, explicit `fsync`/`FlushFileBuffers`, or native Windows/MSVC validation.
