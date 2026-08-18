# Pipeline Definition and Execution Plan Architecture v1

**Iteration:** 012  
**Status:** Candidate  
**Scope:** Pipeline definition, DAG validation, deterministic execution-plan snapshots, and built-in demo worker execution.

## 1. Architectural boundaries

### Domain

The Domain layer owns:

- `PipelineStep`
- `PipelineDefinition`
- dependency invariants
- cycle detection
- deterministic topological ordering
- finite, positive step-weight validation

It has no JSON, filesystem, worker-process, SQLite, or Application dependencies.

### Application

The Application layer owns:

- `ExecutionPlan`
- `ExecutionPlanStep`
- `PipelinePlanner`
- weighted overall-progress calculation
- `IExecutionPlanStore`
- `PipelinePreparationService`

Application receives a valid Domain pipeline and produces an immutable, ordered plan. It does not know how the plan is serialized or where it is stored.

### Pipeline Protocol

The isolated `pipeline_protocol` module owns wire/storage DTOs and strict JSON schema v1 codecs for:

- pipeline-definition documents
- execution-plan documents

The codec enforces bounded input, UTF-8 validity, duplicate/unknown-field rejection, exact types, and supported schema versions.

### Infrastructure

Infrastructure owns:

- JSON pipeline-definition loading
- execution-plan snapshot publication
- canonical path and symlink checks
- worker launch argument propagation

Snapshots are published beneath:

```text
<project>/.biocore/runtime/jobs/<job-id>/execution-plan-r<revision>.json
```

### Worker executable

`biocore-worker` remains a bootstrapper. When `--execution-plan` is present, it:

1. validates the project root and snapshot containment;
2. loads and validates the execution-plan document;
3. verifies launch job ID and revision against the snapshot;
4. executes supported built-in demo modules sequentially;
5. emits lifecycle NDJSON through the accepted Worker Protocol v1.

Without `--execution-plan`, the previous deterministic lifecycle self-test behavior remains available for compatibility and diagnostics.

## 2. Pipeline definition invariants

A pipeline definition must:

- use schema version 1;
- contain 1–256 steps;
- use unique, nonblank step identifiers;
- use finite weights greater than zero;
- contain no self-dependency;
- contain no duplicate dependency per step;
- reference only existing steps;
- contain no dependency cycle.

Topological ordering is deterministic: when multiple nodes are ready, their original definition order is preserved.

## 3. Execution-plan invariants

An execution plan contains:

- schema version;
- job ID and launch revision;
- pipeline ID and version;
- topologically ordered steps;
- normalized step weights summing to 1.0.

Dependencies of each plan step must occur earlier in the plan. Weighted progress rejects unknown step IDs, non-finite/out-of-range values, and progress on a step whose dependencies are incomplete.

## 4. JSON schema and parser limits

The custom schema-specific parser intentionally supports only the JSON constructs required by pipeline documents. It enforces:

- maximum document size: 1 MiB;
- maximum nesting depth: 16;
- maximum steps: 256;
- maximum dependencies per step: 64;
- strict UTF-8 and Unicode escape validation;
- no raw NUL characters;
- no duplicate object fields;
- no unknown object fields;
- exact schema versions and field types.

Boolean values and arbitrary parameter objects are not part of schema v1.

## 5. Snapshot publication

Execution plans are serialized to a same-directory temporary file and published by rename. Existing snapshots are immutable and are never overwritten.

The store rejects:

- unsafe job path components;
- symlinked `.biocore`, `runtime`, `jobs`, or job directories;
- project roots that are not canonical existing directories;
- paths escaping the project root.

Rollback removes only paths created by the current store operation.

## 6. Built-in demo execution

Iteration 012 supports only these deterministic module identifiers:

- `org.biocore.demo.validate`
- `org.biocore.demo.scan`
- `org.biocore.demo.report`

The included demo pipeline uses weights `0.2`, `0.6`, and `0.2`, producing weighted progress `0.2`, `0.8`, and `1.0`.

Unknown modules produce a `failed` lifecycle event and native exit code 3. No real biological analysis is performed.

## 7. Deferred boundaries

Iteration 012 deliberately does not provide:

- plugin manifests or module discovery;
- arbitrary parameters, input bindings, or output bindings;
- parallel DAG execution;
- automatic scheduler-to-plan preparation wiring;
- database persistence of execution-plan metadata;
- snapshot retention or orphan cleanup;
- content signatures or checksums;
- `fsync`/`FlushFileBuffers` durability;
- fully race-free no-overwrite publication on every platform;
- real bioinformatics tools.
