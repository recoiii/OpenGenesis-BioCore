# Local Web Server and Composition Root v1

## 1. Purpose

Iteration 019 introduces the first local web-server boundary for OpenGenesis-BioCore. The goal is not to ship the
browser frontend yet; it is to make the already accepted Core services reachable through a narrow,
authenticated, versioned localhost API while preserving the Clean Architecture dependency direction.

The implementation is deliberately split into two layers:

1. a framework-neutral Presentation contract (`LocalApiController`, `ILocalWebServer`), which is fully
   compiled and tested in every validation matrix; and
2. an optional Drogon adapter (`DrogonLocalWebServer`), selected only when CMake finds Drogon.

Drogon is not installed in the Iteration 019 validation environment. The framework-neutral contract,
composition root, and unavailable-backend behavior are therefore runtime-tested here, while the
Drogon-specific translation source is reviewed against current upstream headers but remains an explicit
native-build boundary.

## 2. Dependency direction

```text
apps/biocore
  -> presentation::ILocalWebServer / LocalApiController
  -> infrastructure adapters
  -> application services
  -> domain

presentation::LocalApiController
  -> application::JobService
  -> application::ArtifactPresentationService
  -> application::IUtcClock

presentation::DrogonLocalWebServer   [optional]
  -> Drogon
  -> presentation::LocalApiController
```

No Drogon type appears in Domain, Application, or Infrastructure.

## 3. Local-only listener policy

`LocalWebServerConfig` defaults to `127.0.0.1`. The final OpenGenesis-BioCore composition root supplies the bind
address as the literal `127.0.0.1`; the Drogon adapter independently rejects any other bind address.
The iteration intentionally does not bind to `0.0.0.0`, LAN interfaces, or IPv6 wildcard addresses.

The default port is 8421. `biocore --serve <project-root> --port <N>` accepts only integer ports in the
range 1..65535.

## 4. Bootstrap authentication

At server startup, Infrastructure generates a 256-bit process-lifetime bootstrap token using the OS
cryptographic random source:

- Windows: `BCryptGenRandom` with `BCRYPT_USE_SYSTEM_PREFERRED_RNG`;
- Linux/POSIX validation path: `getrandom()` with EINTR-safe retry.

The token is encoded as 64 lowercase hexadecimal characters and printed once to the local process
terminal. The public health route is the only unauthenticated REST route. All other REST routes require
an exact `Authorization: Bearer <token>` value. Token comparison is constant-time with respect to the
maximum of the supplied and expected lengths.

The token is not persisted to SQLite or a project file.

## 5. Request bounds and strict routing

The framework-neutral API enforces:

- target <= 2048 bytes,
- Authorization <= 4096 bytes,
- body <= 16 KiB,
- valid UTF-8 and no NUL data,
- `/api/v1/...` version prefix,
- no query string, fragment, percent-encoded path, or double-slash routing in v1,
- safe path atoms restricted to ASCII letters, digits, `.`, `_`, and `-`, with `.`/`..` rejected.

Create-job JSON is deliberately narrow. Only `analysisId`, `pipelineId`, `pipelineVersion`, and
`priority` are accepted; duplicate and unknown fields are rejected. The v1 parser supports the common
JSON string escapes but deliberately does not support `\\u` escapes yet.

## 6. REST surface

Public:

- `GET /api/v1/health`

Authenticated:

- `GET /api/v1/jobs`
- `POST /api/v1/jobs`
- `GET /api/v1/jobs/{job}`
- `GET /api/v1/jobs/{job}/artifacts`
- `GET /api/v1/jobs/{job}/artifacts/{step}/{port}`
- `GET /api/v1/jobs/{job}/artifacts/{step}/{port}/download`
- `GET /api/v1/jobs/{job}/report.json`
- `GET /api/v1/jobs/{job}/report.html`

Responses use `Cache-Control: no-store` and `X-Content-Type-Options: nosniff`; rendered HTML retains the
strict report CSP established in Iteration 018.

## 7. Download boundary

The API never serializes `ArtifactDownloadDescriptor::content_path` into JSON or HTML. It transfers the
verified path only through an internal `LocalFileBody` value to the server adapter.

When Drogon is available, the adapter opens the verified file and exposes it through Drogon's streaming
response API. The stream is capped to the already verified byte count. An inability to open the file
after verification returns a conflict response instead of serializing or redirecting to the path.

This narrows, but does not eliminate, the verification-to-open TOCTOU window. Handle-based hash-and-serve
from the same immutable native file handle remains deferred.

## 8. Startup composition and recovery ordering

`run_local_server()` is the final server composition support function used by `apps/biocore/main.cpp`.
For an available backend it:

1. validates/canonicalizes the project root and existing `.biocore/project.sqlite`,
2. applies project migrations,
3. creates SQLite repositories,
4. creates Job, artifact-presentation, cleanup, and retention services,
5. invokes `ProjectRecoveryService::recover()`,
6. creates the CSPRNG bootstrap token and `LocalApiController`,
7. only then invokes `ILocalWebServer::run()`.

The integration test observes a stale `running` Job as `interrupted` from inside the fake server's
`run()` call, proving recovery precedes listening/serving.

If the binary is compiled without Drogon, `run_local_server()` returns exit code 3 before validating or
opening a project. This is fail-closed and prevents a build that lacks the server dependency from
pretending that localhost serving is available.

## 9. Optional Drogon build contract

CMake performs `find_package(Drogon CONFIG QUIET)`.

- If found, the Drogon source is compiled and `BIOCORE_HAS_DROGON=1`.
- If absent, the unavailable adapter is compiled and `BIOCORE_HAS_DROGON=0`.
- Builders that require a usable web server can set `BIOCORE_REQUIRE_DROGON=ON`; configure then fails if
  Drogon is missing.

Iteration 019 also fixes project-option initialization ordering so warning/sanitizer interface targets
are created after OpenGenesis-BioCore options have been defined.

## 10. WebSocket foundation

The transport-neutral controller provides an authenticated `jobs.snapshot` read model. The optional
Drogon adapter registers `/api/v1/ws/jobs`, requires an Authorization header during upgrade, sends an
initial snapshot, and accepts only a text `snapshot` command for an on-demand refresh.

This is intentionally not yet a browser-ready live push bus. Mainstream browser WebSocket APIs cannot
freely set an Authorization header, and putting the bootstrap token in a query string would leak it into
URLs/logs/history. A same-origin session-cookie or explicitly reviewed WebSocket subprotocol handshake,
plus event-driven broadcast, is deferred rather than weakening the token boundary.

## 11. Runtime boundary

The local server composition root does not automatically start `JobScheduler`/`WorkerRuntime` yet.
`POST /api/v1/jobs` persists a Job, but automatic pipeline-definition selection, execution-plan
preparation, plugin registry wiring, and scheduler activation are not yet joined into a single API
transaction. Starting the scheduler without that preparation boundary could launch a queued Job without
an execution plan, so Iteration 019 intentionally remains fail-safe rather than pretending end-to-end
analysis launch is complete.

## 12. Explicit boundaries

- Drogon is absent in the validation container; native Drogon compile/run is not claimed.
- Current Drogon upstream interfaces used by the adapter were source-reviewed, including original-path
  and query access, regex HTTP handler registration, manually registered WebSocket controllers, and
  streaming response construction.
- Browser frontend/static-file serving is not implemented.
- WebSocket is authenticated snapshot request/response, not automatic live push.
- Browser-friendly session establishment is deferred; query-token authentication is intentionally not
  used.
- WorkerRuntime/Scheduler activation from the server composition root is deferred until pipeline
  preparation can be wired atomically.
- Artifact streaming still has a verification-to-open TOCTOU window.
- Native Windows/MSVC web-server behavior is unvalidated.
- TLS is intentionally absent because Iteration 019 binds only to localhost; remote serving is not a
  supported mode.
