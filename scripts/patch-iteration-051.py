#!/usr/bin/env python3
from pathlib import Path

path = Path("src/application/include/biocore/application/artifact_presentation_service.hpp")
text = path.read_text(encoding="utf-8")
old = "    std::uint64_t attempt_number{1U};\n"
new = "    std::int64_t attempt_number{1};\n"
if text.count(old) != 1:
    raise RuntimeError("expected exactly one unsigned attempt_number declaration")
path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")
Path(__file__).unlink()
print("Iteration 051 attempt type patch complete")
