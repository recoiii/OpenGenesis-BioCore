#!/usr/bin/env python3
"""Seal the 089 candidate and embed every changed file in four thematic parts."""
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
BASELINE = "c82d2ef0210f4a74d191b2c27c9c73d640544ebf"
BASELINE_TREE = "e3087bde6594b603db4f4a06152c3519fa6f7e06"
TITLES = ["Scope, baseline and contracts", "Implementation delta and source context",
          "Tests, regressions and failure scenarios", "Evidence, limits and verdict request"]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path, required=True)
    evidence_mode = parser.add_mutually_exclusive_group(required=True)
    evidence_mode.add_argument("--ci-run-id")
    evidence_mode.add_argument("--local-only", action="store_true",
                               help="Prepare a draft with local evidence; CI remains an open gate")
    args = parser.parse_args()
    base.BASELINE_NAME = "accepted/iteration-088"
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
    for entry in entries:
        if entry.path.startswith("docs/"):
            index = 0
        elif entry.path.startswith(("tests/",)):
            index = 2
        elif entry.path.startswith((".github/", "scripts/")):
            index = 3
        else:
            index = 1
        groups[index].append(entry)
        assignments[entry.path] = index + 1
    if any(not group for group in groups):
        raise RuntimeError("each thematic part must contain changed source")
    args.output.mkdir(parents=True, exist_ok=True)
    archive = args.output / "OpenGenesis-BioCore-iteration-089-source-CANDIDATE.zip"
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
        if f"COMMIT={commit}" not in summary or "100% tests passed" not in summary or "292" not in summary:
            raise RuntimeError(f"missing/incorrect candidate test evidence: {lane}")
    evidence = "\n\n".join(f"### {path.name}\n\n```text\n{path.read_text().rstrip()}\n```"
                             for path in evidence_files)
    evidence_status = ("LOCAL DRAFT — remote CI has NOT run; not ready for final iteration ACCEPT"
                       if args.local_only else
                       f"Same-candidate CI: https://github.com/recoiii/OpenGenesis-BioCore/actions/runs/{args.ci_run_id}")
    generated = []
    for index, group in enumerate(groups):
        body = f"""# OpenGenesis-BioCore v0.6.0-dev — Iteration 089 — Part {index + 1:02d}/04

## {TITLES[index]}

- Candidate commit: `{commit}`
- Candidate tree: `{tree}`
- Frozen baseline: `accepted/iteration-088` / `{BASELINE}`
- Baseline tree: `{BASELINE_TREE}`
- Full source ZIP SHA-256: `{source_hash}`
- Evidence status: {evidence_status}
- Status: CANDIDATE; independent Gemini review pending; NOT accepted or frozen.
- Linux evidence is included; native Windows and package closure are not claimed for 089.

Read all four parts and the source ZIP. Evaluate the roadmap scope and contracts,
not only green tests. 089 changes development identity, documents, fixtures and CI;
it does not implement the registry/API or new scientific algorithms.
Project owner/developer: Recep Çelik. ChatGPT: AI-assisted implementation and review preparation.

{base.manifest(entries, assignments)}
"""
        body += "\n".join(base.render_entry(entry) for entry in group)
        if index == 1:
            body += "\n## Unchanged scientific source context (authoritative source ZIP also supplied)\n"
            for path in ("src/domain/source/case_control_association.cpp",
                         "src/domain/source/multi_sample_matrix_vcf.cpp",
                         "src/application/include/biocore/application/batch_results_service.hpp"):
                body += f"\n### {path}\n\n```cpp\n{base.git('show', 'HEAD:' + path)}\n```\n"
        if index == 3:
            body += "\n## Executed evidence\n\n" + evidence
            body += """

## Open limitations and verdict

For a LOCAL DRAFT, four-lane CI is an explicit open gate; review can identify
findings, but final iteration acceptance is premature until CI evidence is attached.
The cohort persistence/mapping/dispatch/UI and hard resource limits are future
iteration contracts, not features tested in 089. Benchmark data describe retained
engine performance on synthetic data. UI latency and full end-to-end report bytes
are not measured yet. No independent roadmap or iteration ACCEPT is assumed.
Return REJECT for a blocking defect, with file/symbol, trigger, impact and correction.
Do not start 090 or create accepted/iteration-089 before an exact-candidate ACCEPT.

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
        path = args.output / f"OpenGenesis-BioCore-iteration-089-GEMINI-review-part-{index + 1:02d}-of-04.md"
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
