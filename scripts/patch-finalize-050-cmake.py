#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]
finalizer = root / "scripts/finalize-iteration-050.py"
text = finalizer.read_text(encoding="utf-8")
old = '''    target_link_libraries(\\n        biocore-plugin-pipeline-contract-hardening-tests\\n        PRIVATE BioCore::infrastructure BioCore::project_warnings BioCore::sanitizers\\n    )'''
new = '''    target_link_libraries(\\n        biocore-plugin-pipeline-contract-hardening-tests\\n        PRIVATE BioCore::infrastructure BioCore::pipeline_protocol BioCore::project_warnings BioCore::sanitizers\\n    )'''
if text.count(old) != 1:
    raise RuntimeError(f"Expected one focused-test linkage block, found {text.count(old)}")
finalizer.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")
Path(__file__).unlink()
