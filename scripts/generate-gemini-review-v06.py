#!/usr/bin/env python3
"""Seal the 096 candidate and embed every changed file in four balanced parts."""
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
BASELINE = "cc60d9bb6e895dfad19c6482c8464b409cf35b8b"
BASELINE_TREE = "4f50edaeb0722716d46dd2c63c5511dcbc6fc82c"
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
    base.BASELINE_NAME = "accepted/iteration-095"
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
    archive = args.output / "OpenGenesis-BioCore-iteration-096-source-CANDIDATE.zip"
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
        if (f"COMMIT={commit}" not in summary or "CTEST_COUNT=358" not in summary or
                "100% tests passed" not in summary or "out of 358" not in summary):
            raise RuntimeError(f"missing/incorrect candidate test evidence: {lane}")
    evidence = "\n\n".join(f"### {path.name}\n\n```text\n{path.read_text().rstrip()}\n```"
                             for path in evidence_files)
    evidence_status = ("LOCAL DRAFT — remote CI has NOT run; not ready for final iteration ACCEPT"
                       if args.local_only else
                       f"Same-candidate CI: https://github.com/recoiii/OpenGenesis-BioCore/actions/runs/{args.ci_run_id}")
    generated = []
    for index, group in enumerate(groups):
        body = f"""# OpenGenesis-BioCore v0.6.0-dev — Iteration 096 — Part {index + 1:02d}/04

## {TITLES[index]}

- Candidate commit: `{commit}`
- Candidate tree: `{tree}`
- Frozen baseline: `accepted/iteration-095` / `{BASELINE}`
- Baseline tree: `{BASELINE_TREE}`
- Full source ZIP SHA-256: `{source_hash}`
- Evidence status: {evidence_status}
- Status: CANDIDATE; independent Gemini review pending; NOT accepted or frozen.
- Linux evidence is included; native Windows and package closure are not claimed for 096.

Read all four parts and the source ZIP. Evaluate the cohort results-explorer/report
contract and the 096 acceptance criteria, not only green tests. Iteration 096 adds
bounded server-side exploration over the immutable 093 snapshot, accepted 094
association statistics and durable 095 execution lineage. Display filters must never
redefine the frozen test universe or recalculate Fisher/BH statistics. Shared and
case/control-specific views are descriptive carrier-presence views. Existing annotation
summaries are linked without creating a second annotation engine. CSV, TSV, JSON and HTML
outputs must carry the same snapshot identity and canonical display-filter definition,
with frozen denominators, exclusions, methods and reference SHA-256 visible. No new
scheduler, execution ledger, HTTP route or integrated browser workflow is introduced.
Project owner/developer: Recep Çelik. ChatGPT: AI-assisted architecture, implementation,
debugging, tests, review preparation and release-process support.

{base.manifest(entries, assignments)}
"""
        body += "\n".join(rendered[entry.path] for entry in group)
        if index == 1:
            body += """\n## Reuse and execution boundary\n\nIteration 096 reuses the accepted immutable 093 snapshot, 094 association semantics,
existing annotation results and the durable 095 execution lineage. It adds bounded explorer/report
application and presentation contracts; it does not rewrite the matrix, association statistics,
annotation engine, scheduler, execution ledger or worker protocol. Integrated HTTP/browser
composition remains Iteration 097. The changed-file manifest above is complete.\n"""
        if index == 3:
            body += "\n## Executed evidence\n\n" + evidence
            body += """

## Open limitations and verdict

For a LOCAL DRAFT, four-lane CI is an explicit open gate; review can identify
findings, but final iteration acceptance is premature until CI evidence is attached.
Integrated project/browser workflow and known-result E2E remain Iteration 097 contracts;
this package must not infer those routes from the presentation renderers. Native Windows
and package evidence remain Iteration 098 final-closure gates. Review pagination bounds,
shared/specific carrier views, frozen-statistics filtering, annotation linkage, identical
snapshot/filter provenance across CSV/TSV/JSON/HTML, export row bounds and drift rejection.
Benchmark data describe retained cohort-contract regression performance on synthetic
data. No independent 096 ACCEPT is assumed. Return REJECT for a blocking defect, with
file/symbol, trigger, impact and correction. Do not start 097 or create
accepted/iteration-096 before an exact-candidate ACCEPT.

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
        path = args.output / f"OpenGenesis-BioCore-iteration-096-GEMINI-review-part-{index + 1:02d}-of-04.md"
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
