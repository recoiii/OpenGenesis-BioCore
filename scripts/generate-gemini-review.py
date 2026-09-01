#!/usr/bin/env python3
"""Generate deterministic multi-part Markdown source packages for Gemini review."""

from __future__ import annotations

import argparse
import hashlib
import subprocess
from dataclasses import dataclass
from pathlib import Path


BASELINE_NAME = "OpenGenesis-BioCore v0.1.0 Iteration 044"
BASELINE_SHA256 = "ba2406131266641bdaf9516b416fa0ca868a6ba4eb12d58c6b31494e491c94ae"


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
        ["git", *args],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
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
        if object_type == "blob":
            data = git("cat-file", "blob", object_sha, binary=True)
            assert isinstance(data, bytes)
        else:
            # Submodules or other non-blob entries are represented by metadata only.
            data = b""
        entries.append(Entry(path, mode, object_type, object_sha, data))
    return entries


def split_entries(entries: list[Entry], part_count: int) -> list[list[Entry]]:
    if part_count < 1:
        raise ValueError("part_count must be positive")
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


def common_header(iteration: int, part_index: int, part_count: int, commit: str, entries: list[Entry]) -> str:
    total_bytes = sum(len(entry.data) for entry in entries)
    scope_path = f"docs/development/ITERATION-{iteration:03d}.md"
    return f"""# OpenGenesis-BioCore v0.2.0-dev — Iteration {iteration:03d} Gemini Independent Validation

## Review identity

- Exact candidate commit: `{commit}`
- Frozen technical baseline: **{BASELINE_NAME}**
- Frozen baseline source SHA-256: `{BASELINE_SHA256}`
- Review package: **{part_count} Markdown parts**
- This file: **Part {part_index:02d} / {part_count:02d}**
- Exact tracked entries represented across the package: **{len(entries)}**
- Exact tracked blob bytes represented across the package: **{total_bytes}**
- Iteration scope and acceptance contract: `{scope_path}`

Read **all {part_count} parts** before returning a verdict. Treat documented claims as claims to verify, not as evidence by themselves. Review the exact source for correctness, regressions, security/data-integrity consequences, scientific correctness where applicable, cross-platform behavior and whether the iteration stayed inside its declared scope.

A blocking defect requires `REJECT`. Do not invent findings merely to be critical. Non-blocking observations may accompany `ACCEPT`.

## Required final response

```text
VERDICT: ACCEPT | REJECT
CONFIDENCE: <0-100>%

BLOCKING FINDINGS:
- NONE

NON-BLOCKING FINDINGS:
- NONE | <findings>

ITERATION TARGET STATUS:
- <target/invariant>: PRESERVED | CLOSED | NOT CLOSED

RATIONALE:
<concise independent rationale>
```

For every substantive finding provide severity (`BLOCKER/HIGH/MEDIUM/LOW`), file/symbol, mechanism, realistic reproduction/evidence, impact, and the smallest robust correction.

---

## Part {part_index:02d} / {part_count:02d}

"""


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--iteration", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--parts", type=int, default=4, choices=(3, 4))
    args = parser.parse_args()

    require_clean_tracked_tree()
    commit = git("rev-parse", "HEAD")
    assert isinstance(commit, str)
    entries = load_entries()
    groups = split_entries(entries, args.parts)

    args.output.mkdir(parents=True, exist_ok=True)
    prefix = f"OpenGenesis-BioCore-iteration-{args.iteration:03d}-GEMINI-review"
    generated: list[Path] = []

    for index, group in enumerate(groups, start=1):
        output_path = args.output / f"{prefix}-part-{index:02d}-of-{args.parts:02d}.md"
        body = common_header(args.iteration, index, args.parts, commit, entries)
        body += "\n".join(render_entry(entry) for entry in group)
        output_path.write_text(body, encoding="utf-8", newline="\n")
        generated.append(output_path)

    checksum_path = args.output / f"{prefix}-SHA256SUMS.txt"
    checksum_lines = []
    for path in generated:
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        checksum_lines.append(f"{digest}  {path.name}")
    checksum_path.write_text("\n".join(checksum_lines) + "\n", encoding="utf-8", newline="\n")

    print(f"ITERATION={args.iteration:03d}")
    print(f"COMMIT={commit}")
    print(f"TRACKED_ENTRIES={len(entries)}")
    print(f"PARTS={args.parts}")
    for path in generated:
        print(f"PART={path}")
    print(f"CHECKSUMS={checksum_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
