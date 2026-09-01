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
    },
    "tests/json_pipeline_io_tests.cpp": {
        'R"({"schemaVersion":1,"id":"org.biocore.demo","name":"Demo","version":"1.0.0","steps":[{"id":"validate","module":"org.biocore.demo.validate","dependsOn":[],"weight":2},{"id":"scan","module":"org.biocore.demo.scan","dependsOn":["validate"],"weight":6},{"id":"report","module":"org.biocore.demo.report","dependsOn":["scan"],"weight":2}]})"':
        'R"({"schemaVersion":2,"id":"org.biocore.demo","name":"Demo","version":"1.0.0","steps":[{"id":"validate","module":"org.biocore.demo.validate","pluginVersion":"0.1.0","dependsOn":[],"weight":2},{"id":"scan","module":"org.biocore.demo.scan","pluginVersion":"0.1.0","dependsOn":["validate"],"weight":6},{"id":"report","module":"org.biocore.demo.report","pluginVersion":"0.1.0","dependsOn":["scan"],"weight":2}]})"',
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

# Existing public pipeline identifiers include internal underscores (for example
# paired_summary and trim_paired). Preserve those stable identifiers while still
# rejecting leading/trailing separators and non-namespaced text.
identity_path = root / "src/domain/source/component_identity.cpp"
identity = identity_path.read_text(encoding="utf-8")
identity_replacements = {
    "value == '-';": "value == '-' || value == '_';",
    "(!segment_has_character && character == '-')":
        "(!segment_has_character && (character == '-' || character == '_'))",
    "previous_hyphen = character == '-';":
        "previous_hyphen = character == '-' || character == '_';",
}
for old, new in identity_replacements.items():
    if identity.count(old) != 1:
        raise RuntimeError(f"component identity patch mismatch: {old}")
    identity = identity.replace(old, new, 1)
identity_path.write_text(identity, encoding="utf-8", newline="\n")

Path(__file__).unlink()
