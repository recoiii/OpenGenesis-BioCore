# OpenGenesis-BioCore v0.1.0 — Iteration 043 Process-Tree Cancellation Closure

## Reason for this iteration

An independent source audit of the exact Iteration 042 R4 source candidate identified one release-blocking
cancellation defect: OpenGenesis-BioCore terminated only the `biocore-worker` process, while the worker can be blocked
waiting for an out-of-process native plugin. Killing only the worker could therefore leave the plugin
running and writing untracked output after the job was reported cancelled.

Iteration 043 is intentionally narrow. It does not add a biological feature or change pipeline semantics.
It closes process-tree ownership for cancellation and adds a regression contract that fails under the old
single-PID termination behavior. The same independent audit also identified a non-blocking test-quality
gap around malformed biological inputs; representative negative fixtures are added here while the source
is already being revalidated.

Historical Iteration 042 R4 source SHA-256:

`39af68f84654d1a844c51d3b2c07e9f304beb3cf929bbd6fe79904de785a3e03`

That hash is now a historical baseline and must not be published as the final v0.1.0 source after this
iteration.

## Process-tree ownership invariant

When OpenGenesis-BioCore reports a running job as cancelled, the worker and every native plugin descendant launched
for that worker must no longer remain running.

### POSIX

- Each worker is created as the leader of a dedicated process group with `POSIX_SPAWN_SETPGROUP`.
- Plugin processes inherit the worker process group.
- Forced cancellation targets the complete group with `killpg(..., SIGKILL)` instead of killing only the
  worker PID.

### Windows

- Each worker gets a dedicated Job Object configured with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`.
- The worker is created suspended, assigned to the Job Object before it can execute user code, and only
  then resumed.
- Native plugin descendants inherit Job Object membership.
- Forced cancellation uses `TerminateJobObject`, terminating the complete worker/plugin tree.
- The Job Object handle remains owned by the supervisor for the tracked worker lifetime.

## Regression contract

`infrastructure.platform_worker_supervisor` now contains a real process-tree probe:

1. the worker probe launches a long-running child process;
2. the test verifies that the child is alive before cancellation;
3. OpenGenesis-BioCore terminates the tracked job;
4. the worker exits with the expected forced-termination code;
5. the child must also be gone;
6. no tracked worker remains.

A deliberate mutation restoring the old POSIX single-PID `kill(worker_pid, SIGKILL)` behavior causes this
regression test to fail. Restoring process-group termination makes it pass again.

## Malformed-input regression coverage

The existing real-plugin integration CTest now also exercises representative defensive failure paths
without increasing the CTest count:

- FASTA with an empty sequence record;
- FASTQ truncated in the quality line;
- truncated/corrupt gzip FASTQ input;
- SAM with an unsupported/malformed CIGAR;
- mapped BAM with zero CIGAR operations;
- VCF with `POS=0`.

Each case must be rejected by the existing parser/algorithm guard rather than being accepted silently.

## Source/Linux validation

The Iteration 043 candidate has completed the following local source gates:

- GCC Debug: **67/67 PASS**
- GCC Release: **67/67 PASS**
- Clang Debug: **67/67 PASS**
- GCC ASan+UBSan: **67/67 PASS**
- targeted process-tree mutation: **KILLED**
- malformed-input negative fixtures: **PASS**
- clean Linux Release install / exact `0.1.0` / Worker Protocol `2` / `--init-project` smoke: **PASS**

Warnings-as-errors remain enabled for the standard GCC/Clang builds.

## Native Windows closure still required

Because Iteration 043 changes the Windows process-launch and termination implementation, the prior
Iteration 042 Windows package and its SHA-256 are historical evidence only. The exact Iteration 043 source
candidate must complete the native Windows Debug/Release, install, CPack and extracted-package closure
before v0.1.0 can be frozen again.

The existing native Windows closure script remains the authoritative gate and now emits an
Iteration 043 evidence bundle.
