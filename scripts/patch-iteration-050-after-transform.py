#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1]

patches = {
    "tests/worker_runtime_sqlite_integration_tests.cpp": {
        '{"validate", "org.biocore.demo.validate", {}, 0.2}': '{"validate", "org.biocore.demo.validate", "0.1.0", {}, 0.2}',
        '{"scan", "org.biocore.demo.scan", {"validate"}, 0.6}': '{"scan", "org.biocore.demo.scan", "0.1.0", {"validate"}, 0.6}',
        '{"report", "org.biocore.demo.report", {"scan"}, 0.2}': '{"report", "org.biocore.demo.report", "0.1.0", {"scan"}, 0.2}',
    },
    "tests/job_submission_service_tests.cpp": {
        '{"validate", "org.biocore.demo.validate", {}, 1.0}': '{"validate", "org.biocore.demo.validate", "0.1.0", {}, 1.0}',
        'domain::PipelineStep{"copy", "org.biocore.demo.copy", {}, 1.0}': 'domain::PipelineStep{"copy", "org.biocore.demo.copy", "0.1.0", {}, 1.0}',
    },
}

for relative, replacements in patches.items():
    path = root / relative
    text = path.read_text(encoding="utf-8")
    for old, new in replacements.items():
        if text.count(old) != 1:
            raise RuntimeError(f"{relative}: expected one legacy fixture: {old}")
        text = text.replace(old, new, 1)
    path.write_text(text, encoding="utf-8", newline="\n")

Path(__file__).unlink()
