#!/usr/bin/env python3
from pathlib import Path

path = Path(__file__).resolve().parents[1] / "tests/export_report_foundation_tests.cpp"
text = path.read_text(encoding="utf-8")
count = text.count('0.2.0-dev')
if count != 2:
    raise SystemExit(f"Expected exactly 2 development identity literals in export test, found {count}")
path.write_text(text.replace('0.2.0-dev', '0.2.0'), encoding="utf-8", newline="\n")
print("Export/report final release identity fixture updated")
