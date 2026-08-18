# OpenGenesis-BioCore Plugin Development Area

OpenGenesis-BioCore supports Plugin Manifest schemas v1 and v2. V1 remains available for process modules
without I/O contracts. V2 adds typed parameters plus input/output port definitions.

A plugin directory contains a strict `plugin.json` and platform-specific executable entrypoints.
Top-level fields are `manifestVersion`, `id`, `name`, `version`, `apiVersion`, `publisher`, and
`modules`. Plugin/module identifiers remain lower-case dotted identifiers and every module must stay
inside its plugin namespace.

Manifest v2 process modules define `parameters`, `inputs`, and `outputs`. Supported parameter types are
`string`, `integer`, `number`, `boolean`, and `enum`; parameters may be required or have defaults and
numeric/enum constraints. Input ports declare accepted OpenGenesis-BioCore file-type labels. Output ports declare
their output file type.

OpenGenesis-BioCore resolves user bindings before execution, writes an immutable invocation JSON snapshot beneath
the project `.biocore/runtime/jobs` tree, and launches the plugin with exactly one dynamic contract
argument: `--invocation <snapshot-path>`. Plugins do not receive arbitrary user CLI fragments.

The demo plugin contains validate, scan, report, fail, and copy modules. `copy` proves typed parameters
and managed-file/output bindings end to end; it is deterministic test infrastructure and does not
perform bioinformatics analysis.

Plugins remain native trusted-local executables in this candidate. Signature verification, sandboxing,
network/filesystem permission enforcement, resource limits, and durable artifact registration are not
yet implemented.
