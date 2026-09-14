#!/usr/bin/env python3
"""Generate deterministic, size-bounded four-part Markdown review packages for v0.3."""

from __future__ import annotations

import argparse
import hashlib
import subprocess
from dataclasses import dataclass
from pathlib import Path

BASELINE_NAME = "OpenGenesis-BioCore v0.2.0"
BASELINE_COMMIT = "31691d65e4e4a7dab9be0730c886e24890043194"
PART_COUNT = 4
MAX_PART_BYTES = 120_000


@dataclass(frozen=True)
class Entry:
    path: str
    status: str
    mode: str
    object_type: str
    object_sha: str
    data: bytes
    deleted: bool = False

    @property
    def sha256(self) -> str:
        return hashlib.sha256(self.data).hexdigest()


def git(*args: str, binary: bool = False) -> bytes | str:
    result = subprocess.run(
        ["git", *args], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    return result.stdout if binary else result.stdout.decode("utf-8").strip()


def require_clean_tracked_tree() -> None:
    subprocess.run(["git", "diff", "--quiet", "HEAD", "--"], check=True)
    subprocess.run(["git", "diff", "--cached", "--quiet", "HEAD", "--"], check=True)


def tree_metadata(ref: str, path: str) -> tuple[str, str, str] | None:
    raw = git("ls-tree", ref, "--", path)
    assert isinstance(raw, str)
    if not raw:
        return None
    metadata, _ = raw.split("\t", 1)
    mode, object_type, object_sha = metadata.split(" ", 2)
    return mode, object_type, object_sha


def changed_paths() -> list[str]:
    raw = git("diff", "--name-only", "-z", BASELINE_COMMIT, "HEAD", "--", binary=True)
    assert isinstance(raw, bytes)
    paths = [item.decode("utf-8") for item in raw.split(b"\0") if item]
    if not paths:
        raise RuntimeError(f"No files differ from the frozen baseline {BASELINE_NAME} ({BASELINE_COMMIT})")
    if len(paths) != len(set(paths)):
        raise RuntimeError("Duplicate changed paths reported by git")
    return sorted(paths)


def load_changed_entries() -> list[Entry]:
    entries: list[Entry] = []
    for path in changed_paths():
        status_raw = git("diff", "--name-status", BASELINE_COMMIT, "HEAD", "--", path)
        assert isinstance(status_raw, str)
        status = status_raw.split("\t", 1)[0] if status_raw else "?"
        metadata = tree_metadata("HEAD", path)
        deleted = metadata is None
        if deleted:
            metadata = tree_metadata(BASELINE_COMMIT, path)
            if metadata is None:
                raise RuntimeError(f"Cannot resolve deleted path metadata: {path}")
            mode, object_type, object_sha = metadata
            data = git("cat-file", "blob", object_sha, binary=True)
        else:
            mode, object_type, object_sha = metadata
            data = git("cat-file", "blob", object_sha, binary=True) if object_type == "blob" else b""
        assert isinstance(data, bytes)
        entries.append(Entry(path, status, mode, object_type, object_sha, data, deleted))
    return entries


def render_entry(entry: Entry) -> str:
    lines = [
        f"===== BEGIN FILE: {entry.path} =====",
        f"CHANGE STATUS: {entry.status}",
        f"GIT MODE: {entry.mode}",
        f"GIT OBJECT TYPE: {entry.object_type}",
        f"GIT OBJECT SHA: {entry.object_sha}",
        f"SHA-256: {entry.sha256}",
        f"BYTES: {len(entry.data)}",
        "",
    ]
    if entry.deleted:
        lines.extend([
            "[DELETED FROM CANDIDATE — baseline content follows for deletion review]",
            "",
        ])
    if entry.object_type != "blob":
        lines.append("[NON-BLOB GIT ENTRY — content intentionally not embedded]")
    else:
        try:
            text = entry.data.decode("utf-8")
        except UnicodeDecodeError as error:
            raise RuntimeError(f"Changed non-UTF-8 blob cannot be embedded safely: {entry.path}") from error
        lines.append(text.rstrip("\n"))
    lines.extend(["", f"===== END FILE: {entry.path} =====", ""])
    return "\n".join(lines)


def assign_groups(entries: list[Entry]) -> tuple[list[list[Entry]], dict[str, int]]:
    groups: list[list[Entry]] = [[] for _ in range(PART_COUNT)]
    group_sizes = [0] * PART_COUNT
    rendered_sizes = {entry.path: len(render_entry(entry).encode("utf-8")) for entry in entries}

    for entry in sorted(entries, key=lambda item: (-rendered_sizes[item.path], item.path)):
        target = min(range(PART_COUNT), key=lambda index: (group_sizes[index], index))
        groups[target].append(entry)
        group_sizes[target] += rendered_sizes[entry.path]

    if any(not group for group in groups):
        raise RuntimeError("Cannot produce exactly four non-empty review parts")

    assignments: dict[str, int] = {}
    for index, group in enumerate(groups, start=1):
        group.sort(key=lambda entry: entry.path)
        for entry in group:
            assignments[entry.path] = index
    return groups, assignments


def manifest(entries: list[Entry], assignments: dict[str, int]) -> str:
    lines = [
        "## Changed-file completeness manifest",
        "",
        f"This package embeds **all {len(entries)} paths changed from `{BASELINE_COMMIT}` to the exact candidate**, exactly once and without content truncation.",
        "Unchanged frozen-baseline files are intentionally not duplicated here. The separately sealed source-candidate ZIP is the authoritative full source tree.",
        "",
        "| Part | Status | Bytes | SHA-256 | Path |",
        "|---:|:---:|---:|---|---|",
    ]
    for entry in sorted(entries, key=lambda item: item.path):
        lines.append(
            f"| {assignments[entry.path]:02d} | `{entry.status}` | {len(entry.data)} | `{entry.sha256}` | `{entry.path}` |"
        )
    lines.extend(["", "**Changed-file completeness: PASS (generator-enforced before upload).**", ""])
    return "\n".join(lines)


def common_header(
    iteration: int,
    part_index: int,
    commit: str,
    entries: list[Entry],
    assignments: dict[str, int],
    ci_run_id: str | None,
) -> str:
    release_identity = "0.3.0" if iteration >= 68 else "0.3.0-dev"
    ci_lines = ""
    if ci_run_id is not None:
        ci_lines = f"""- GitHub Actions validation run: `{ci_run_id}`
- CI prerequisite status: **PASSED before package generation**
- Required gates: GCC Debug, GCC Release, Clang Debug, GCC ASan+UBSan, exact CTest inventory and iteration-specific evidence jobs
"""

    return f"""# OpenGenesis-BioCore v{release_identity} — Iteration {iteration:03d} Gemini Independent Validation

## Review identity

- Exact candidate commit: `{commit}`
- Frozen baseline: **{BASELINE_NAME}**
- Frozen baseline commit: `{BASELINE_COMMIT}`
{ci_lines}- Review package: **exactly {PART_COUNT} Markdown source parts**
- This file: **Part {part_index:02d} / {PART_COUNT:02d}**
- Hard package limit: **{MAX_PART_BYTES} bytes per Markdown part**
- Iteration scope and acceptance contract: `docs/development/ITERATION-{iteration:03d}.md`

Read all four parts before returning a verdict. The Markdown package is an exact, complete embedding of the candidate's **baseline-relative changed files**, not a lossy copy of the entire repository. No changed file may be omitted, shortened, split mid-file, or represented with an ellipsis. Treat documented claims as claims to verify.

A blocking defect requires `REJECT`. Do not freeze or accept an iteration merely because CI is green.

{manifest(entries, assignments)}
## Required final response

```text
VERDICT: ACCEPT | REJECT
CONFIDENCE: <0-100>%

BLOCKING FINDINGS:
- NONE | <findings>

NON-BLOCKING FINDINGS:
- NONE | <findings>

ITERATION TARGET STATUS:
- <target/invariant>: PRESERVED | CLOSED | NOT CLOSED

RATIONALE:
<concise independent rationale>
```

For every substantive finding provide severity (`BLOCKER/HIGH/MEDIUM/LOW`), file/symbol, mechanism, realistic evidence/trigger, impact and the smallest robust correction.

---

## Part {part_index:02d} / {PART_COUNT:02d}

"""


def iteration_evidence(directory: Path | None, iteration: int) -> str:
    if directory is None:
        return ""
    paths = sorted(path for path in directory.iterdir() if path.is_file() and path.suffix == ".txt")
    if not paths:
        raise RuntimeError(f"No text evidence files found in: {directory}")
    outputs: list[str] = []
    for path in paths:
        outputs.append(
            f"### `{path.name}`\n\n```text\n{path.read_text(encoding='utf-8').rstrip()}\n```\n"
        )
    return f"\n---\n\n## GitHub Actions Iteration {iteration:03d} evidence\n\n" + "\n".join(outputs)


def verify_generated(
    generated: list[Path], entries: list[Entry], benchmark_text: str
) -> None:
    if len(generated) != PART_COUNT:
        raise RuntimeError("Review package must contain exactly four Markdown parts")
    combined = "\n".join(path.read_text(encoding="utf-8") for path in generated)
    for entry in entries:
        begin = f"===== BEGIN FILE: {entry.path} ====="
        end = f"===== END FILE: {entry.path} ====="
        if combined.count(begin) != 1 or combined.count(end) != 1:
            raise RuntimeError(f"Changed file is missing or duplicated in review package: {entry.path}")
        if entry.object_type == "blob" and not entry.deleted:
            text = entry.data.decode("utf-8").rstrip("\n")
            if text not in combined:
                raise RuntimeError(f"Changed file content was not embedded verbatim: {entry.path}")
    if benchmark_text and benchmark_text not in generated[2].read_text(encoding="utf-8"):
        raise RuntimeError("Benchmark evidence is missing from part 03")
    for path in generated:
        size = path.stat().st_size
        if size > MAX_PART_BYTES:
            raise RuntimeError(f"Review part exceeds {MAX_PART_BYTES} bytes: {path.name} ({size})")


def main() -> int:
    global BASELINE_NAME, BASELINE_COMMIT

    parser = argparse.ArgumentParser()
    parser.add_argument("--iteration", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--ci-run-id")
    parser.add_argument("--baseline-name", default=BASELINE_NAME)
    parser.add_argument("--baseline-commit", default=BASELINE_COMMIT)
    parser.add_argument("--evidence-dir", type=Path)
    args = parser.parse_args()

    BASELINE_NAME = args.baseline_name
    BASELINE_COMMIT = args.baseline_commit

    require_clean_tracked_tree()
    commit = git("rev-parse", "HEAD")
    assert isinstance(commit, str)
    entries = load_changed_entries()
    groups, assignments = assign_groups(entries)
    evidence = iteration_evidence(args.evidence_dir, args.iteration)

    args.output.mkdir(parents=True, exist_ok=True)
    prefix = f"OpenGenesis-BioCore-iteration-{args.iteration:03d}-GEMINI-review"
    generated: list[Path] = []

    for index, group in enumerate(groups, start=1):
        path = args.output / f"{prefix}-part-{index:02d}-of-04.md"
        body = common_header(args.iteration, index, commit, entries, assignments, args.ci_run_id)
        body += "\n".join(render_entry(entry) for entry in group)
        if index == 3:
            body += evidence
        path.write_text(body, encoding="utf-8", newline="\n")
        generated.append(path)

    verify_generated(generated, entries, evidence)

    checksum_path = args.output / f"{prefix}-SHA256SUMS.txt"
    checksum_path.write_text(
        "".join(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n" for path in generated),
        encoding="utf-8",
        newline="\n",
    )

    print(f"ITERATION={args.iteration:03d}")
    print(f"COMMIT={commit}")
    print(f"CHANGED_FILES={len(entries)}")
    print(f"PARTS={PART_COUNT}")
    for path in generated:
        print(f"PART_BYTES={path.name}:{path.stat().st_size}")
    print("COMPLETENESS=PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
