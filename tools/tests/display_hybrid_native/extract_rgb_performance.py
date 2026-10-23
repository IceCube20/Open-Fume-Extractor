"""Extract production scheduling/counter helpers without pulling in the renderer."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[3]
source = (root / "Module/DisplayModule_800x480/DisplayModule_800x480.ino").read_text(encoding="utf-8-sig")


def function(name):
    match = re.search(r"^static void " + name + r"\([^;]*?\)\s*\{", source, re.M)
    pos, depth = match.end(), 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[match.start():pos]


assert "display_benchmark_reset_counters(now);" in function("display_benchmark_finish")
assert "display_benchmark_reset_counters(millis());" in function("display_benchmark_back_event")
Path(sys.argv[1]).write_text("\n\n".join(function(name) for name in (
    "display_benchmark_reset_counters", "draw_update_progress_throttled")), encoding="utf-8")
