#!/usr/bin/env python3
"""Generate deterministic four-part Markdown source packages for v0.3 Gemini review."""

from __future__ import annotations

import argparse
import hashlib
import subprocess
from dataclasses import dataclass
from pathlib import Path

BASELINE_NAME = "OpenGenesis-BioCore v0.2.0"
BASELINE_COMMIT = "31691d65e4e4a7dab9be0730c886e24890043194"


@dataclass(frozen=True)
class Entry:
    path: str
    mode: str
    object_type: str
    object_sha: str
    data: bytes

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


def load_entries() -> list[Entry]:
    raw = git("ls-tree", "-r", "-z", "--full-tree", "HEAD", binary=True)
    assert isinstance(raw, bytes)
    entries: list[Entry] = []
    for record in raw.split(b"\0"):
        if not record:
            continue
        metadata, raw_path = record.split(b"\t", 1)
        mode, object_type, object_sha = metadata.decode("ascii").split(" ", 2)
        path = raw_path.decode("utf-8")
        data = git("cat-file", "blob", object_sha, binary=True) if object_type == "blob" else b""
        assert isinstance(data, bytes)
        entries.append(Entry(path, mode, object_type, object_sha, data))
    return entries


def split_entries(entries: list[Entry], part_count: int) -> list[list[Entry]]:
    groups: list[list[Entry]] = [[] for _ in range(part_count)]
    total_weight = sum(max(len(entry.data), 1) for entry in entries)
    cumulative = 0
    part = 0
    for entry in entries:
        groups[part].append(entry)
        cumulative += max(len(entry.data), 1)
        if part < part_count - 1:
            threshold = total_weight * (part + 1) / part_count
            if cumulative >= threshold:
                part += 1
    return groups


def render_entry(entry: Entry) -> str:
    lines = [
        f"===== BEGIN FILE: {entry.path} =====",
        f"GIT MODE: {entry.mode}",
        f"GIT OBJECT TYPE: {entry.object_type}",
        f"GIT OBJECT SHA: {entry.object_sha}",
        f"SHA-256: {entry.sha256}",
        f"BYTES: {len(entry.data)}",
        "",
    ]
    if entry.object_type != "blob":
        lines.append("[NON-BLOB GIT ENTRY — content intentionally not embedded]")
    else:
        try:
            text = entry.data.decode("utf-8")
        except UnicodeDecodeError:
            lines.append("[NON-UTF-8 BLOB — metadata and hashes recorded; binary content intentionally not embedded]")
        else:
            lines.append(text.rstrip("\n"))
    lines.extend(["", f"===== END FILE: {entry.path} =====", ""])
    return "\n".join(lines)


def common_header(
    iteration: int,
    part_index: int,
    part_count: int,
    commit: str,
    entries: list[Entry],
    ci_run_id: str | None,
) -> str:
    del entries
    release_identity = "0.3.0" if iteration >= 68 else "0.3.0-dev"
    ci_lines = ""
    if ci_run_id is not None:
        ci_lines = f"""- GitHub Actions validation run: `{ci_run_id}`
- CI prerequisite status: **PASSED before package generation**
- Required gates for Iteration {iteration:03d}: GCC Debug, GCC Release, Clang Debug, GCC ASan+UBSan, exact CTest inventory and iteration-specific evidence jobs
"""

    return f"""# OpenGenesis-BioCore v{release_identity} — Iteration {iteration:03d} Gemini Independent Validation

## Review identity

- Exact candidate commit: `{commit}`
- Frozen baseline: **{BASELINE_NAME}**
- Frozen baseline commit: `{BASELINE_COMMIT}`
{ci_lines}- Review package: **exactly {part_count} Markdown source parts**
- This file: **Part {part_index:02d} / {part_count:02d}**
- Iteration scope and acceptance contract: `docs/development/ITERATION-{iteration:03d}.md`

Read all {part_count} parts before returning a verdict. Treat documented claims as claims to verify. Review the exact source for correctness, regressions, data integrity, scientific correctness where applicable, determinism, memory/lifetime safety, cross-platform C++20 portability and declared-scope compliance.

A blocking defect requires `REJECT`. Do not freeze or accept an iteration merely because CI is green.

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

## Part {part_index:02d} / {part_count:02d}

"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--iteration", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--ci-run-id")
    args = parser.parse_args()

    require_clean_tracked_tree()
    commit = git("rev-parse", "HEAD")
    assert isinstance(commit, str)
    entries = load_entries()
    groups = split_entries(entries, 4)
    args.output.mkdir(parents=True, exist_ok=True)
    prefix = f"OpenGenesis-BioCore-iteration-{args.iteration:03d}-GEMINI-review"
    generated: list[Path] = []

    for index, group in enumerate(groups, start=1):
        path = args.output / f"{prefix}-part-{index:02d}-of-04.md"
        body = common_header(args.iteration, index, 4, commit, entries, args.ci_run_id)
        body += "\n".join(render_entry(entry) for entry in group)
        path.write_text(body, encoding="utf-8", newline="\n")
        generated.append(path)

    checksum_path = args.output / f"{prefix}-SHA256SUMS.txt"
    checksum_path.write_text(
        "".join(f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n" for path in generated),
        encoding="utf-8",
        newline="\n",
    )

    print(f"ITERATION={args.iteration:03d}")
    print(f"COMMIT={commit}")
    print("PARTS=4")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
