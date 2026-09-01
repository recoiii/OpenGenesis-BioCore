#!/usr/bin/env python3
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
finalizer = root / "scripts/finalize-iteration-050.py"
text = finalizer.read_text(encoding="utf-8")
pattern = re.compile(
    r'replace_once\(\n    "tests/filesystem_pipeline_catalog_tests\.cpp",.*?\n\)\n\n# json_pipeline_io',
    re.DOTALL,
)
replacement = '''replace_once(
    "tests/filesystem_pipeline_catalog_tests.cpp",
    'R"({"schemaVersion":1,"id":"org.biocore.demo.validation","name":"Demo","version":"0.1.0","steps":[{"id":"validate","module":"org.biocore.demo.validate","dependsOn":[],"weight":1.0}]})"',
    'R"({"schemaVersion":2,"id":"org.biocore.demo.validation","name":"Demo","version":"0.1.0","steps":[{"id":"validate","module":"org.biocore.demo.validate","pluginVersion":"0.1.0","dependsOn":[],"weight":1.0}]})"',
)

# json_pipeline_io'''
text, count = pattern.subn(replacement, text, count=1)
if count != 1:
    raise RuntimeError(f"Unable to patch filesystem pipeline fixture block: {count}")
finalizer.write_text(text, encoding="utf-8", newline="\n")
Path(__file__).unlink()
