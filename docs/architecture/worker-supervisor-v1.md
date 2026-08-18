# Worker Supervisor Architecture v1

## Status

Iteration 009 accepted. Extended by Iteration 010 worker-event transport.

## Responsibility split

- `JobScheduler` remains Application-owned and decides which queued job may launch.
- `IWorkerSupervisor` remains the Application port.
- `PlatformWorkerSupervisor` is an Infrastructure adapter that owns OS process creation and
  process-identity tracking.
- `biocore-worker` validates the fixed worker protocol v1 CLI contract.

Application and Domain code do not include POSIX or Windows process APIs.

## Launch contract

The worker receives exactly these arguments, in this order:

```text
biocore-worker
  --job-id <job-id>
  --project-root <canonical-project-root>
  --job-revision <non-negative-revision>
```

The fixed ordering is intentional. The adapter builds a native argv array and never constructs a
shell command.

## Platform implementations

### POSIX

- Uses `posix_spawn` with an exact executable path.
- Passes a null-terminated `char*` argv array.
- Inherits the current environment.
- Tracks the returned PID.
- Uses non-blocking `waitpid(..., WNOHANG)` to collect exit status and release zombies.

### Windows

- Uses `CreateProcessW` with `lpApplicationName` set to the exact canonical executable path.
- Converts UTF-8 protocol strings to UTF-16 with strict invalid-sequence rejection.
- Quotes every argument using Microsoft backslash/quote rules.
- Closes the initial thread handle immediately and tracks the process handle plus PID.
- Uses `WaitForSingleObject(..., 0)` and `GetExitCodeProcess` for non-blocking exit collection.

The pure Windows quoting implementation is compiled and tested on Linux as well, so empty
arguments, embedded quotes, spaces, and trailing backslashes remain covered even before a Windows
runtime validation host is available.

## Configuration invariants

The adapter requires:

- an absolute, existing, canonical, non-symlink regular worker executable;
- execute permission on POSIX;
- an absolute, existing, canonical, non-symlink project directory;
- a non-blank, NUL-free job ID no longer than 128 bytes;
- a non-negative persisted job revision.

## Process registry

The adapter stores one native process identity per active/tracked job ID. A placeholder registry
entry is allocated before the OS spawn call, so allocation failure cannot leave an untracked child.
If spawning fails, the placeholder is removed before the typed error escapes.

Duplicate launch requests for a currently tracked job are rejected. Completed workers remain
tracked until `reap_exited()` is called. Runtime owners must poll that method regularly. The adapter
destructor makes one final no-throw reap attempt and closes remaining Windows process handles.

## Explicit boundaries

Iteration 009 does not provide:

- worker ready acknowledgement;
- heartbeat, progress, completion, or failure message handling;
- cancellation or graceful shutdown;
- process-group/job-object ownership;
- zombie/orphan recovery after application crash;
- persistence of PID/process identity in SQLite;
- cross-process scheduler leadership;
- CPU, memory, or disk resource limits.

A successful `launch()` only means that the operating system accepted the process-creation request.
