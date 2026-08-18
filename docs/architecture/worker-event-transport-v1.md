# Worker Event Transport Architecture v1

## Status

Iteration 010 candidate.

## Purpose

The worker-event transport carries structured lifecycle events from an isolated `biocore-worker`
process back to OpenGenesis-BioCore without allowing process, JSON, pipe, or SQLite concerns to leak into Domain
or Application policy.

The transport is local, shell-free, newline framed, versioned, and bounded. Worker stdout is a
machine protocol channel. Worker stderr is a separate human-readable diagnostic channel.

## Boundary split

- `worker_protocol` owns the wire schema, strict JSON codec, and NDJSON framing.
- `PlatformWorkerSupervisor` owns native stdout/stderr pipes and process-output draining.
- `worker_event_mapper` translates a validated wire event into the Application lifecycle model.
- `WorkerEventIngestionSession` owns lifecycle ordering and Job state persistence.
- `JobService` and `IJobRepository` continue to own optimistic revision checks.
- Domain owns Job transition and progress invariants only.

Application does not include or link the Worker Protocol module. Infrastructure performs the mapping
at the boundary.

## Worker stdout contract

Each stdout line is exactly one UTF-8 JSON object with these base fields:

```text
protocolVersion
 type
 jobId
 jobRevision
 sequence
 timestampUtc
```

Supported lifecycle types are:

```text
ready
heartbeat
progress
log
completed
failed
```

Control messages such as `cancel`, `shutdown`, `ping`, and `pong` are not valid worker stdout
lifecycle events.

The codec rejects:

- unsupported protocol versions;
- duplicate or unknown JSON fields;
- nested arrays or objects;
- invalid UTF-8, raw NUL bytes, malformed escapes, and invalid surrogate pairs;
- non-finite or out-of-range progress values;
- type-incompatible fields;
- lines larger than 64 KiB.

## NDJSON framing

`NdjsonFramer` accepts arbitrary byte chunks and preserves partial lines between reads. It supports
LF and CRLF framing and can finalize one trailing line at EOF. Buffer growth is bounded before bytes
are appended. Once a framing failure occurs for a process, protocol decoding is disabled while the
pipe continues to be drained so a child cannot block on a full stdout pipe.

## stdout and stderr separation

- stdout is decoded only as lifecycle NDJSON;
- stderr is collected as bounded diagnostic lines;
- malformed stdout is reported as a typed `WorkerProtocolIssue` and is never reclassified as a
  diagnostic message;
- raw malformed protocol lines are not exposed in the public result, reducing accidental leakage of
  biological or local-path content.

## Lifecycle ingestion

A `WorkerEventIngestionSession` is bound to one expected Job ID and the persisted revision used at
launch. It enforces:

- exact Job ID and launch-revision identity;
- strictly contiguous sequence numbers beginning at 1;
- `ready` before progress or successful completion;
- nondecreasing progress for a running Job;
- one terminal lifecycle event;
- optimistic-concurrency-safe persistence;
- terminal event and native process exit-code agreement.

Events are applied as follows:

| Event | Application effect |
|---|---|
| `ready` | `preparing -> running` |
| `heartbeat` | observed only; timeout policy is deferred |
| `progress` | persist progress and optional active step |
| `log` | observed only; durable log storage is deferred |
| `completed` | `running -> completed`, progress `1.0` |
| `failed` | eligible active state -> `failed` |

If a process exits without a terminal lifecycle event, finalization moves a worker-slot-occupying Job
to `interrupted`. If a terminal event exists but disagrees with the native exit code, finalization
fails with a lifecycle error rather than silently accepting contradictory state.

## Native pipe ownership

### POSIX

The supervisor creates dedicated stdout and stderr pipes, marks parent read ends non-blocking, and
uses `posix_spawn_file_actions_adddup2` to attach child file descriptors. Parent and child close the
opposite ends after spawn.

### Windows

The supervisor uses `STARTUPINFOEXW` and `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`. Only the intended
stdout pipe, stderr pipe, and controlled `NUL` stdin handle are inherited by the child. General
inheritance of unrelated application handles is not permitted by design.

The Windows branch is source-reviewed but has not yet been compiled or run on a Windows host.

## Explicit boundaries

Iteration 010 does not provide:

- heartbeat timeout policy or heartbeat persistence;
- durable worker log storage;
- stdin control transport, cancellation, pause, resume, or graceful shutdown;
- autonomous background pipe readers or reapers;
- process groups on POSIX or Job Objects on Windows;
- PID/handle persistence, orphan recovery, or cross-process scheduler leadership;
- resource limits;
- complete file-descriptor/handle substitution TOCTOU elimination.

The runtime owner must poll process output and exit state frequently enough to avoid pipe
backpressure.
