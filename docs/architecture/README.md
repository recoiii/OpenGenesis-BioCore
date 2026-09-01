# Architecture Boundary Map

```text
apps/biocore (composition root)
  -> presentation
  -> infrastructure
  -> application
  -> domain

apps/worker (composition root)
  -> worker_protocol / pipeline_protocol / plugin_protocol
  -> infrastructure
  -> application
  -> domain

presentation
  -> application
  -> domain

infrastructure
  -> application
  -> domain
  -> worker_protocol / pipeline_protocol / plugin_protocol
  -> SQLite
  -> filesystem / clock / random source / native process APIs

application
  -> domain

domain
  -> no OpenGenesis-BioCore module
```

## Boundary rule

Framework, database, filesystem, clock, and random-generator implementations remain outside Domain and Application. Application owns the ports and use-case orchestration; Infrastructure implements those ports. Executables remain composition roots and do not contain persistence business logic.

## Iteration history

- Iteration 001 verified the dependency structure, worker protocol, health serialization, and job state machine.
- Iteration 002 attached the first SQLite adapter without reversing any dependency direction.
- Iteration 003 added `ProjectService` and the ID, UTC-clock, and canonical-path ports required for deterministic project registration.
- Iteration 004 added the transactional project-workspace port and the filesystem workspace adapter without exposing filesystem details to Application.
- Iteration 005 adds a distinct project-local SQLite database, migration history, singleton metadata, and initial managed-file/job/settings schemas inside the Infrastructure boundary.
- Iteration 006 adds the `Job` aggregate, Application-owned `IJobRepository` and `JobService`, optimistic revisions, and the project-local SQLite job adapter.
- Iteration 007 adds transactional managed-file copying and project-local managed-file persistence.
- Iteration 008 adds Application-owned scheduling policy, worker-slot accounting, and the abstract worker-supervisor launch boundary.
- Iteration 009 adds shell-free native worker process launch and process-identity tracking.
- Iteration 010 adds strict lifecycle NDJSON, native stdout/stderr pipes, and Application-owned event ingestion.
- Iteration 011 adds the autonomous Application runtime, monotonic heartbeat recovery, forced termination, and reserved native-slot accounting.
- Iteration 012 adds Domain pipeline DAG validation, Application execution plans, strict pipeline JSON, and immutable plan snapshots.
- Iteration 013 adds plugin discovery, module registry resolution, manifest rebinding, and shell-free native plugin execution.
- Iteration 014 adds typed plugin parameters, file-port bindings, execution-plan schema v3, and immutable per-step invocation snapshots.
- Iteration 015 adds Worker Protocol v2 artifact events, Core-side output reinspection, and transactional generated-output provenance persistence.
- Iteration 016 adds step-level artifact buffering, all-or-none sibling persistence, and project-local partial-output quarantine after failed/interrupted execution.
- Iteration 017 adds durable generated-output progress checkpoints, conservative stale-Job startup recovery, and bounded quarantine retention.
- Iteration 018 adds generated-output SHA-256 integrity, verified artifact download preparation, deterministic artifact read models, and JSON/HTML report rendering foundations.
- Iteration 019 adds the authenticated localhost API contract, OS-CSPRNG bootstrap tokens, optional Drogon transport adapter, loopback-only final server composition root, and recovery-before-serve lifecycle wiring.
- Iteration 020 adds exact pipeline catalog submission, atomic queued-Job/execution-plan association, scheduler plan gating, separate API/runtime SQLite ownership, and automatic WorkerRuntime activation.
- Iteration 034 adds the native ungapped reference-alignment plugin contract, SAM/JSON/TSV artifacts, single/paired FASTQ alignment, reverse-complement search, mismatch bounds, and explicit correctness-first performance limits without expanding Core architecture.
- Iteration 041 hardens long-running native execution with in-plugin heartbeats, strict liveness semantics, bounded worker stream draining/background observation retention, and an authenticated cancellation path that finalizes terminated work as `cancelled`.
- Iteration 047 adds schema-v7 atomic Job failure evidence, typed worker/process/timeout/recovery diagnostics, and durable API/report/browser presentation.

See [Data Persistence v1](data-persistence-v1.md), [Project Creation v1](project-creation-v1.md), [Project Workspace v1](project-workspace-v1.md), [Project Database v1](project-database-v1.md), and [Job Repository v1](job-repository-v1.md), [Managed File Registration v1](managed-file-registration-v1.md), [Job Scheduler v1](job-scheduler-v1.md), [Worker Supervisor v1](worker-supervisor-v1.md), and
[Worker Event Transport v1](worker-event-transport-v1.md), [Autonomous Worker Runtime v1](autonomous-worker-runtime-v1.md),
[Pipeline Definition and Execution Plan v1](pipeline-definition-execution-plan-v1.md),
[Plugin Discovery and Module Registry v1](plugin-discovery-module-registry-v1.md), and
[Plugin Parameter and File Binding v1](plugin-parameter-file-binding-v1.md), and
[Generated Output Artifact Registration v1](generated-output-artifact-registration-v1.md), and
[Step-Level Batch Artifact Persistence v1](step-level-batch-artifact-persistence-v1.md), and
[Durable Session Recovery and Quarantine Retention v1](durable-session-recovery-v1.md), and
[Artifact Presentation and Integrity v1](artifact-presentation-integrity-v1.md), and
[Local Web Server and Composition Root v1](local-web-server-v1.md), and
[Prepared Job Submission and Runtime Activation v1](prepared-job-runtime-activation-v1.md), and
[Native Ungapped Reference Alignment v1](native-ungapped-alignment-v1.md), and
[Worker Runtime Resilience v2](worker-runtime-resilience-v2.md), and
[Job Failure Diagnostics v1](job-failure-diagnostics-v1.md) for the boundary documents.
