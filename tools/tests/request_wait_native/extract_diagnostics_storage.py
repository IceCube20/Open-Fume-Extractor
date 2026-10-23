"""Extract actual allocation, initialization and counter methods for fault injection."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[3]
header = (root / "OpenFumeExtractorMaster/src/MasterScheduler.h").read_text(encoding="utf-8")
source = (root / "OpenFumeExtractorMaster/src/MasterScheduler.cpp").read_text(encoding="utf-8")


def balanced(text, start):
    pos = text.index("{", start) + 1
    depth = 1
    while depth:
        depth += (text[pos] == "{") - (text[pos] == "}")
        pos += 1
    return text[start:pos]


def method(name):
    match = re.search(r"^[^\n]+\bMasterScheduler::" + name + r"\(", source, re.M)
    return balanced(source, match.start())


fixture = """
class MasterScheduler {
public:
STRUCT
  static const uint8_t BUS_DIAG_ADDR_COUNT = 0x60;
  BusModuleDiag* bus_module_diag_ = nullptr;
  struct { bool lastTxWasNetwork() { return network; }
           bool lastRxWasNetwork() { return network; } bool network=false; } link_;
  uint32_t ofe_tx_frames_=0, ofe_rx_frames_=0;
  uint64_t ofe_tx_wire_bytes_=0, ofe_rx_wire_bytes_=0;
  bool initBusDiagnostics();
  int16_t busDiagIndex(uint8_t) const;
  bool busModuleDiag(uint8_t, BusModuleDiag&) const;
  static uint16_t ofeWireBytesApprox(const Frame&);
  void busDiagRecordTx(const Frame&);
  void busDiagRecordRx(const Frame&);
};
""".replace("STRUCT", balanced(header, header.index("  struct BusModuleDiag")) + ";")
assert "BusModuleDiag* bus_module_diag_ = nullptr;" in header
assert "initBusDiagnostics();" in method("begin")
parts = [fixture] + [method(name) for name in (
    "initBusDiagnostics", "busDiagIndex", "busModuleDiag", "ofeWireBytesApprox",
    "busDiagRecordTx", "busDiagRecordRx")]
Path(sys.argv[1]).write_text("\n\n".join(parts), encoding="utf-8")
