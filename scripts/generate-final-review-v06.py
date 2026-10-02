#!/usr/bin/env python3
"""Generate the exact Iteration 098 four-part final review package."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys

spec = importlib.util.spec_from_file_location(
    "review_v05", Path(__file__).with_name("generate-gemini-review-v05.py")
)
assert spec and spec.loader
base = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = base
spec.loader.exec_module(base)

ITERATION = 98
BASELINE_NAME = "accepted/iteration-097"
BASELINE_COMMIT = "5a85bac8f36ab0bdee84b06d9f850bdb04b9e964"
BASELINE_TREE = "26ba7edf28a89bc735882250348faa1226f6bd43"
EXPECTED_TESTS = 359
MAX_PART_BYTES = 120_000
TITLES = (
    "Release candidate delta — partition A",
    "Release candidate delta — partition B",
    "Release candidate delta — partition C",
    "Final CI, Windows/package evidence and partition D",
)


def require_lane(evidence_dir: Path, name: str, commit: str) -> None:
    path = evidence_dir / f"{name}.txt"
    text = path.read_text(encoding="utf-8")
    required = (
        f"COMMIT={commit}",
        "VERSION=0.6.0",
        f"CTEST_COUNT={EXPECTED_TESTS}",
        "100% tests passed",
        f"out of {EXPECTED_TESTS}",
    )
    missing = [item for item in required if item not in text]
    if missing:
        raise RuntimeError(f"{name} evidence is incomplete: {missing}")


def require_windows(evidence_dir: Path, source_sha256: str) -> None:
    path = evidence_dir / "windows-final-closure-summary.txt"
    payload = json.loads(path.read_text(encoding="utf-8-sig"))
    expected = {
        "release": "0.6.0",
        "sourceSha256": source_sha256,
        "debugCTestCount": EXPECTED_TESTS,
        "debugCTestResult": "PASS",
        "releaseCTestCount": EXPECTED_TESTS,
        "releaseCTestResult": "PASS",
        "installedVersion": "0.6.0",
        "installedPluginCount": 9,
        "installedPipelineCount": 13,
        "installedWorkflowTemplateCount": 1,
        "extractedVersion": "0.6.0",
        "extractedPluginCount": 9,
        "extractedPipelineCount": 13,
        "extractedWorkflowTemplateCount": 1,
        "systemDirectoryPayload": "NONE",
        "result": "PASS",
    }
    mismatches = {
        key: (payload.get(key), value)
        for key, value in expected.items()
        if payload.get(key) != value
    }
    if mismatches:
        raise RuntimeError(f"Windows final closure evidence mismatch: {mismatches}")


def require_closure_evidence(evidence_dir: Path, commit: str) -> None:
    for name in (
        "v04-compatibility-ctest.txt",
        "v05-compatibility-ctest.txt",
        "v06-cohort-closure-ctest.txt",
    ):
        text = (evidence_dir / name).read_text(encoding="utf-8")
        if "100% tests passed" not in text:
            raise RuntimeError(f"Failed closure CTest evidence: {name}")
    summary = (evidence_dir / "final-linux-closure-summary.txt").read_text(encoding="utf-8")
    for marker in (f"COMMIT={commit}", "VERSION=0.6.0", "SCHEMA=18", "GIAB=PASS"):
        if marker not in summary:
            raise RuntimeError(f"Missing final Linux closure marker: {marker}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path, required=True)
    parser.add_argument("--source-archive", type=Path, required=True)
    parser.add_argument("--ci-run-id", required=True)
    args = parser.parse_args()

    base.BASELINE_NAME = BASELINE_NAME
    base.BASELINE_COMMIT = BASELINE_COMMIT
    base.require_clean_tracked_tree()

    if base.git("rev-parse", f"{BASELINE_COMMIT}^{{tree}}") != BASELINE_TREE:
        raise RuntimeError("Frozen Iteration 097 baseline tree mismatch")
    subprocess.run(["git", "merge-base", "--is-ancestor", BASELINE_COMMIT, "HEAD"], check=True)

    commit = base.git("rev-parse", "HEAD")
    tree = base.git("rev-parse", "HEAD^{tree}")
    assert isinstance(commit, str) and isinstance(tree, str)

    source_archive = args.source_archive.resolve()
    if not source_archive.is_file():
        raise RuntimeError("Exact source candidate archive is missing")
    source_sha256 = hashlib.sha256(source_archive.read_bytes()).hexdigest()

    for lane in (
        "linux-gcc-debug",
        "linux-gcc-release",
        "linux-clang-debug",
        "linux-gcc-asan-ubsan",
    ):
        require_lane(args.evidence_dir, lane, commit)
    require_windows(args.evidence_dir, source_sha256)
    require_closure_evidence(args.evidence_dir, commit)

    entries = base.load_changed_entries()
    if len(entries) < 4:
        raise RuntimeError("Iteration 098 final delta unexpectedly has fewer than four changed paths")
    rendered = {entry.path: base.render_entry(entry) for entry in entries}
    groups: list[list] = [[], [], [], []]
    assignments: dict[str, int] = {}
    costs = [0, 0, 0, 24_000]
    support = [
        entry for entry in entries
        if entry.path.startswith((".github/", "scripts/"))
    ]
    for entry in support:
        groups[3].append(entry)
        assignments[entry.path] = 4
        costs[3] += len(rendered[entry.path].encode("utf-8"))
    remaining = [entry for entry in entries if entry not in support]
    remaining.sort(key=lambda item: (-len(rendered[item.path].encode("utf-8")), item.path))
    for entry in remaining:
        target = min(range(4), key=lambda index: (costs[index], index))
        groups[target].append(entry)
        assignments[entry.path] = target + 1
        costs[target] += len(rendered[entry.path].encode("utf-8"))
    if any(not group for group in groups):
        raise RuntimeError("Each review part must contain changed source")

    evidence_files = sorted(path for path in args.evidence_dir.iterdir() if path.is_file() and path.suffix == ".txt")
    evidence = "\n\n".join(
        f"### {path.name}\n\n```text\n{path.read_text(encoding='utf-8-sig').rstrip()}\n```"
        for path in evidence_files
    )

    args.output.mkdir(parents=True, exist_ok=True)
    archive_out = args.output / "OpenGenesis-BioCore-iteration-098-source-CANDIDATE.zip"
    shutil.copyfile(source_archive, archive_out)

    generated: list[Path] = []
    for index, group in enumerate(groups):
        manifest = (
            base.manifest(entries, assignments)
            if index == 3
            else "The complete changed-file manifest and part assignments are in Part 04.\n"
        )
        body = f"""# OpenGenesis-BioCore v0.6.0 — Iteration 098 — Part {index + 1:02d}/04

## {TITLES[index]}

- Exact candidate commit: `{commit}`
- Exact candidate tree: `{tree}`
- Frozen baseline: `{BASELINE_NAME}` / `{BASELINE_COMMIT}`
- Frozen baseline tree: `{BASELINE_TREE}`
- Full source ZIP SHA-256: `{source_sha256}`
- Same-candidate final CI: https://github.com/recoiii/OpenGenesis-BioCore/actions/runs/{args.ci_run_id}
- Status: FINAL CANDIDATE; NOT frozen/tagged/released until independent Gemini and Claude ACCEPT.
- Project owner/developer: Recep Çelik.
- ChatGPT contribution: AI-assisted architecture, implementation support, debugging, test/review specification,
  evidence preparation, documentation and release-process support.

Iteration 098 adds no scientific feature. Review the final release identity, exact-source evidence chain,
Linux regressions, retained scientific/migration/recovery/cohort gates, native Windows MSVC Debug/Release,
clean install, portable CPack ZIP, plugin/pipeline/template inventory, system-DLL exclusion and documentation.
The accepted Iteration 097 cohort/statistical implementation must remain unchanged except for release-closure
infrastructure and metadata.

{manifest}
"""
        body += "\n".join(rendered[entry.path] for entry in group)
        if index == 3:
            body += f"""
## Final executed evidence

{evidence}

## Final acceptance rule

A blocking source-identity, regression, scientific-contract, Windows/package, migration/recovery,
documentation or evidence defect requires REJECT. Green CI alone is not sufficient. Both Gemini and
Claude must independently accept this exact source identity before `accepted/iteration-098`, tag
`v0.6.0`, release publication, stable-ref movement or Zenodo deposition.

Return:

```text
VERDICT: ACCEPT | REJECT
CONFIDENCE: <0-100>%
SOURCE IDENTITY: MATCH | MISMATCH
SCOPE/BASELINE/CONTRACTS: CLOSED | OPEN
SCIENTIFIC REUSE/FIXTURES: CLOSED | OPEN
REGRESSION/RESOURCE EVIDENCE: CLOSED | OPEN
WINDOWS/PACKAGING: CLOSED | OPEN
RELEASE METADATA/DOCS: CLOSED | OPEN
BLOCKING FINDINGS: NONE | <numbered findings>
NON-BLOCKING FINDINGS: NONE | <numbered findings>
RATIONALE: <concise technically specific rationale>
```
"""
        path = args.output / f"OpenGenesis-BioCore-iteration-098-GEMINI-review-part-{index + 1:02d}-of-04.md"
        path.write_text(body, encoding="utf-8", newline="\n")
        generated.append(path)

    base.verify_generated(generated, entries, "")
    for path in generated:
        if path.stat().st_size > MAX_PART_BYTES:
            raise RuntimeError(f"Review part exceeds {MAX_PART_BYTES} bytes: {path}")

    checksum = args.output / "SHA256SUMS.txt"
    checksum.write_text(
        "".join(
            f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n"
            for path in [archive_out, *generated]
        ),
        encoding="utf-8",
        newline="\n",
    )
    print(f"COMMIT={commit}")
    print(f"TREE={tree}")
    print(f"SOURCE_SHA256={source_sha256}")
    print(f"CHANGED_FILES={len(entries)}")
    print("PARTS=4")
    print("COMPLETENESS=PASS")
    for path in generated:
        print(f"PART_BYTES={path.name}:{path.stat().st_size}")


if __name__ == "__main__":
    main()
