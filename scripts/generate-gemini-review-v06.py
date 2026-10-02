#!/usr/bin/env python3
"""Seal the 095 candidate and embed every changed file in four balanced parts."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import sys

# Reuse the accepted blob reader/completeness verifier; do not inherit v0.5 claims.
spec = importlib.util.spec_from_file_location("review_v05", Path(__file__).with_name("generate-gemini-review-v05.py"))
assert spec and spec.loader
base = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = base
spec.loader.exec_module(base)
BASELINE = "b8a9b175d38cb2b3968a4e160c1e0f650557c038"
BASELINE_TREE = "db1c67eeba2e5d0cc311b1814e23812f15c413c5"
TITLES = ["Candidate source delta — partition A", "Candidate source delta — partition B",
          "Candidate source delta — partition C", "CI evidence, limits and source partition D"]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path, required=True)
    evidence_mode = parser.add_mutually_exclusive_group(required=True)
    evidence_mode.add_argument("--ci-run-id")
    evidence_mode.add_argument("--local-only", action="store_true",
                               help="Prepare a draft with local evidence; CI remains an open gate")
    args = parser.parse_args()
    base.BASELINE_NAME = "accepted/iteration-094"
    base.BASELINE_COMMIT = BASELINE
    base.require_clean_tracked_tree()
    if base.git("rev-parse", f"{BASELINE}^{{tree}}") != BASELINE_TREE:
        raise RuntimeError("baseline tree mismatch")
    subprocess.run(["git", "merge-base", "--is-ancestor", BASELINE, "HEAD"], check=True)
    commit = base.git("rev-parse", "HEAD")
    tree = base.git("rev-parse", "HEAD^{tree}")
    entries = base.load_changed_entries()
    groups: list[list] = [[], [], [], []]
    assignments = {}
    rendered = {entry.path: base.render_entry(entry) for entry in entries}
    # Reserve space in part 04 for CI evidence, then greedily balance the exact
    # rendered changed-source bytes across all four parts. Review completeness is
    # preserved by the manifest and verify_generated() below.
    costs = [0, 0, 0, 30000]
    support = [
        entry for entry in entries
        if entry.path.startswith((".github/", "scripts/"))
    ]
    for entry in support:
        groups[3].append(entry)
        assignments[entry.path] = 4
        costs[3] += len(rendered[entry.path].encode("utf-8"))
    remaining = [entry for entry in entries if entry not in support]
    remaining.sort(
        key=lambda entry: (-len(rendered[entry.path].encode("utf-8")), entry.path)
    )
    for entry in remaining:
        index = min(range(4), key=lambda candidate: (costs[candidate], candidate))
        groups[index].append(entry)
        assignments[entry.path] = index + 1
        costs[index] += len(rendered[entry.path].encode("utf-8"))
    if any(not group for group in groups):
        raise RuntimeError("each review part must contain changed source")
    args.output.mkdir(parents=True, exist_ok=True)
    archive = args.output / "OpenGenesis-BioCore-iteration-095-source-CANDIDATE.zip"
    subprocess.run(["git", "archive", "--format=zip", "--prefix=OpenGenesis-BioCore/",
                    f"--output={archive}", "HEAD"], check=True)
    source_hash = hashlib.sha256(archive.read_bytes()).hexdigest()
    evidence_files = sorted(args.evidence_dir.glob("*.txt"))
    if not evidence_files:
        raise RuntimeError("missing evidence")
    # Same-candidate markers must be present in each lane summary, not inferred
    # merely from a caller-provided run ID.
    lanes = ("linux-gcc-debug",) if args.local_only else (
        "linux-gcc-debug", "linux-gcc-release", "linux-clang-debug", "linux-gcc-asan-ubsan")
    for lane in lanes:
        summary = (args.evidence_dir / f"{lane}.txt").read_text()
        if (f"COMMIT={commit}" not in summary or "CTEST_COUNT=350" not in summary or
                "100% tests passed" not in summary or "out of 350" not in summary):
            raise RuntimeError(f"missing/incorrect candidate test evidence: {lane}")
    evidence = "\n\n".join(f"### {path.name}\n\n```text\n{path.read_text().rstrip()}\n```"
                             for path in evidence_files)
    evidence_status = ("LOCAL DRAFT — remote CI has NOT run; not ready for final iteration ACCEPT"
                       if args.local_only else
                       f"Same-candidate CI: https://github.com/recoiii/OpenGenesis-BioCore/actions/runs/{args.ci_run_id}")
    generated = []
    for index, group in enumerate(groups):
        body = f"""# OpenGenesis-BioCore v0.6.0-dev — Iteration 095 — Part {index + 1:02d}/04

## {TITLES[index]}

- Candidate commit: `{commit}`
- Candidate tree: `{tree}`
- Frozen baseline: `accepted/iteration-094` / `{BASELINE}`
- Baseline tree: `{BASELINE_TREE}`
- Full source ZIP SHA-256: `{source_hash}`
- Evidence status: {evidence_status}
- Status: CANDIDATE; independent Gemini review pending; NOT accepted or frozen.
- Linux evidence is included; native Windows and package closure are not claimed for 095.

Read all four parts and the source ZIP. Evaluate the accepted cohort execution contract and
the 095 acceptance criteria, not only green tests. Iteration 095 adds durable,
idempotent execution-attempt lineage around the immutable analysis snapshot accepted in
093 and the snapshot-bound association integration accepted in 094. Attempt reservation
is persisted before scheduler handoff; duplicate initial/retry requests collapse by
idempotency key and payload digest; retries create new attempt and Job identities with
explicit parent lineage; cancellation intent is durable; pinned inputs are revalidated
before submit/retry; recovery never promotes an orphan or merely completed Job to a
completed cohort analysis. Completion additionally requires a durable verified result
manifest identity and SHA-256. Existing Job submission/service/runtime abstractions are
reused rather than replaced by a second scheduler.
Project owner/developer: Recep Çelik. ChatGPT: AI-assisted architecture, implementation,
debugging, tests, review preparation and release-process support.

{base.manifest(entries, assignments)}
"""
        body += "\n".join(rendered[entry.path] for entry in group)
        if index == 1:
            body += """\n## Reuse and execution boundary\n\nIteration 095 reuses the accepted immutable 093 snapshot, 094 association semantics, the existing\nJob submission/service abstraction and the established worker/runtime state model. It adds an\nadditive schema-v18 attempt ledger and application execution coordinator; it does not rewrite the\nmatrix, association statistics, scheduler or worker protocol. Result exploration and report\ngeneration are not introduced here. The changed-file manifest above is complete.\n"""
        if index == 3:
            body += "\n## Executed evidence\n\n" + evidence
            body += """

## Open limitations and verdict

For a LOCAL DRAFT, four-lane CI is an explicit open gate; review can identify
findings, but final iteration acceptance is premature until CI evidence is attached.
Result exploration/reporting remain Iteration 096 contracts and integrated HTTP/browser
runtime composition remains Iteration 097; this package must not infer those surfaces
from the application execution service. Native Windows and package evidence remain
Iteration 098 final-closure gates. Review idempotency, crash reconciliation,
cancellation persistence, retry parent lineage, snapshot/input integrity, schema-v18
atomicity and the rule that a Job completion alone is never cohort completion.
Benchmark data describe retained cohort-contract regression performance on synthetic
data. No independent 095 ACCEPT is assumed. Return REJECT for a blocking defect, with
file/symbol, trigger, impact and correction. Do not start 096 or create
accepted/iteration-095 before an exact-candidate ACCEPT.

```text
VERDICT: ACCEPT | REJECT
CONFIDENCE: <0-100>%
SOURCE IDENTITY: MATCH | MISMATCH
SCOPE/BASELINE/CONTRACTS: CLOSED | NOT CLOSED
SCIENTIFIC REUSE/FIXTURES: CLOSED | NOT CLOSED
REGRESSION/RESOURCE EVIDENCE: CLOSED | NOT CLOSED
BLOCKING FINDINGS: NONE | <findings>
NON-BLOCKING FINDINGS: NONE | <findings>
RATIONALE: <independent rationale>
```
"""
        path = args.output / f"OpenGenesis-BioCore-iteration-095-GEMINI-review-part-{index + 1:02d}-of-04.md"
        path.write_text(body, encoding="utf-8", newline="\n")
        generated.append(path)
    base.verify_generated(generated, entries, "")
    (args.output / "SHA256SUMS.txt").write_text("".join(
        f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n"
        for path in [archive, *generated]), encoding="utf-8", newline="\n")
    print(f"COMMIT={commit}\nTREE={tree}\nSOURCE_SHA256={source_hash}\nPARTS=4\nCOMPLETENESS=PASS")
    for path in generated:
        print(f"PART_BYTES={path.name}:{path.stat().st_size}")


if __name__ == "__main__":
    main()
