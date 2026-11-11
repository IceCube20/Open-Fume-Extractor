"""Extract production scheduler methods into a hardware-free native fixture."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
DEST = ROOT / "tools/tests/build-checks/system-automation"
DEST.mkdir(parents=True, exist_ok=True)
header = (ROOT / "OpenFumeExtractorMaster/src/MasterScheduler.h").read_text(encoding="utf-8")
source = (ROOT / "OpenFumeExtractorMaster/src/MasterScheduler.cpp").read_text(encoding="utf-8")
enums = []
for name in ("InputSystemState", "SystemOutput", "ExtractorAction"):
    enums.append(re.search(r"  enum " + name + r" : uint8_t \{.*?\n  };", header, re.S).group())
(DEST / "scheduler_enums.inc").write_text("\n".join(enums), encoding="utf-8")
methods = []
for name in ("systemInputMask", "systemInputActive", "setLogicSystemOutput", "requestDisplayWake"):
    match = re.search(r"(?:uint32_t|bool|void) MasterScheduler::" + name + r"\(.*?\n}\n", source, re.S)
    if not match:
        raise ValueError(name)
    methods.append(match.group())
(DEST / "scheduler_methods.inc").write_text("\n".join(methods), encoding="utf-8")
logic = (ROOT / "OpenFumeExtractorMaster/src/WebLogic.inc.h").read_text(encoding="utf-8")
methods = []
for name in ("logic_extractor_action_from_signal", "logic_output_is_event_signal", "logic_apply_output_signal"):
    match = re.search(r"static (?:uint8_t|bool) " + name + r"\(.*?\n}\n", logic, re.S)
    if not match:
        raise ValueError(name)
    methods.append(match.group())
(DEST / "logic_methods.inc").write_text("\n".join(methods), encoding="utf-8")
