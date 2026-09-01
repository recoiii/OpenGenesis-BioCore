# Iteration 053 — Cross-platform, Resource & Performance Hardening

## Scope

- Preserve project database schema v8 and Worker Protocol v2.
- Preserve all scientific algorithms, thresholds, plugin versions, pipeline IDs and output formats.
- Bound scheduler worker concurrency consistently for every caller.
- Keep default worker concurrency at 2 while allowing explicit local tuning from 1 through 64.
- Remove per-subscriber lifecycle JSON payload duplication by sharing one immutable serialized message across subscriber queues.
- Preserve existing subscriber and pending-message fail-closed limits.
- Keep process-tree containment, bounded stdout/stderr drains, managed-file SHA-256 streaming and localhost security unchanged.

## Resource contract

`JobScheduler::maximum_supported_concurrent_jobs` is the single application-level hard ceiling. The CLI and local-server bootstrap reject values outside the same range before worker scheduling can begin.

`WorkerLifecycleEventBroadcastHub` serializes each lifecycle event once. Subscriber queues retain `shared_ptr<const string>` references, so payload storage is not multiplied by the number of connected browser consumers. Slow subscribers are still dropped when their pending queue reaches the existing bound.

## Validation target

- CTest floor: at least 75 tests.
- New `integration.resource_performance_hardening` stress/contract test.
- GCC Debug, GCC Release, Clang Debug and GCC ASan+UBSan.
- Exact `0.2.0-dev` identity and existing browser/plugin-pin guardrails.
- Gemini review package must contain exactly four Markdown parts.
