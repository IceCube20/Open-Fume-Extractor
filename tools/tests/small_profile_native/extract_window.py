"""Extract actual small-display counter/reset/report functions for native tests."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[3]
name, output = sys.argv[1:]
source = (root / "Module" / name / (name + ".ino")).read_text(encoding="utf-8-sig")


def function(name):
    match = re.search(r"^static void " + name + r"\([^;]*?\)\s*\{", source, re.M)
    end = source.index("\n}\n", match.start()) + 3
    return source[match.start():end]


finish = function("display_benchmark_finish")
assert finish.index("display_benchmark_reset_counters(now);") > finish.index("debug_printf(")
assert function("display_benchmark_back_event").count("display_benchmark_reset_counters(millis());") == 1
Path(output).write_text("\n\n".join(function(name) for name in (
    "display_benchmark_reset_counters", "perf_report_if_due", "display_benchmark_back_event")), encoding="utf-8")
