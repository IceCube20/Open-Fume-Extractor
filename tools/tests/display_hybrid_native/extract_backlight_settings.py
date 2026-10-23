"""Extract the production master command for host-side protocol tests."""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[3]
source = (root / "OpenFumeExtractorMaster/src/MasterScheduler.cpp").read_text(encoding="utf-8")
start = source.index("bool MasterScheduler::setDisplaySettings(")
end = source.index("\nbool MasterScheduler::setUniversalProfile(", start)
Path(sys.argv[1]).write_text(source[start:end], encoding="utf-8")
