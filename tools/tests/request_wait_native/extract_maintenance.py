"""Build a native fixture from the production maintenance accounting methods."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[3]
source = (root / "OpenFumeExtractorMaster/src/MasterScheduler.cpp").read_text(encoding="utf-8")
header = (root / "OpenFumeExtractorMaster/src/MasterScheduler.h").read_text(encoding="utf-8")


def balanced(text, start):
    pos = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[pos] == "{") - (text[pos] == "}")
        pos += 1
    return text[start:pos]


def function(name):
    match = re.search(r"^[^\n]+\b" + name + r"\(", source, re.M)
    if not match:
        raise RuntimeError(name)
    return balanced(source, match.start())


structs = "\n".join(balanced(header, header.index("  struct " + name)) + ";"
                     for name in ("BusModuleDiag", "TraceStats"))
enum = re.search(r"  enum class RequestDiagnosticsKind[^;]+;", header)[0]
fixture = """
class MasterScheduler {
public:
STRUCTS
ENUM
  ModuleRegistry registry_;
  BusModuleDiag bus_module_diag_[256];
  TraceStats trace_stats_;
  bool module_fw_active_ = false;
  uint8_t module_fw_target_ = ADDR_INVALID;
  unsigned offline_callbacks = 0;
  uint16_t trace_budget = 0;
  const char* trace_text = nullptr;
  RequestDiagnosticsKind requestDiagnosticsKind(uint8_t, uint8_t);
  bool recordMaintenanceTimeout(uint8_t, uint8_t, uint8_t, uint32_t, RequestDiagnosticsKind);
  void recordRequestTimeout(uint8_t, uint8_t, uint8_t, uint32_t, bool);
  static int16_t busDiagIndex(uint8_t addr) { return addr; }
  bool traceMatches(uint8_t) const { return true; }
  void traceLog(uint8_t, int, uint8_t, uint8_t, const uint8_t*, uint8_t,
                uint16_t budget, const char* text, uint8_t) {
    trace_budget = budget; trace_text = text;
  }
  void refreshPowerSaveActive() {}
  void selectRoles() { ++offline_callbacks; }
};
""".replace("STRUCTS", structs).replace("ENUM", enum)

parts = [fixture, function("bus_diag_record_timeout_class")]
parts.extend(function(name) for name in (
    "requestDiagnosticsKind", "recordMaintenanceTimeout", "recordRequestTimeout"))

# Check that both receive paths actually use the tested production policy,
# and only validated INFO (not an arbitrary response) clears recovery early.
assert "recordMaintenanceTimeout(dst, cmd, req.seq, timeout_ms, diagnostics_kind)" in function("MasterScheduler::request")
begin = function("moduleFwBegin")
assert "rec->display_async_pending = false;" in begin
assert "rec->display_async_timeout_ms = 0;" in begin
assert "firmware_recovery_pending = true" in function("invalidateModuleAfterFirmwareUpdate")
assert "firmware_recovery_pending = false" in function("readInfo")
assert "firmware_recovery_pending = false" not in function("MasterScheduler::request")
assert "invalidateModuleAfterFirmwareUpdate" not in function("moduleFwAbort")
Path(sys.argv[1]).write_text("\n\n".join(parts), encoding="utf-8")
