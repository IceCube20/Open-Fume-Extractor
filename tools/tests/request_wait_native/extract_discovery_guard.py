"""Extract the production discovery reservation and wait loop for fake-clock tests."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[3]
source = (root / "OpenFumeExtractorMaster/src/MasterScheduler.cpp").read_text(encoding="utf-8")
header = (root / "OpenFumeExtractorMaster/src/MasterScheduler.h").read_text(encoding="utf-8")


def function(name):
    start = source.index("void MasterScheduler::" + name + "(")
    pos = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == "{") - (source[pos] == "}")
        pos += 1
    return source[start:pos]


constant = re.search(r"^#define HOTPLUG_DISCOVERY_WINDOW_MS .+$", header, re.M)[0]
assert "link_.setPhysicalSendGuard(this" in function("begin")
assert "waitForDiscoveryWindow();" in function("begin")
Path(sys.argv[1]).write_text(
    "\n\n".join((constant, function("reserveDiscoveryWindow"), function("waitForDiscoveryWindow"))),
    encoding="utf-8",
)
