from pathlib import Path

root = Path(__file__).resolve().parents[3]
module = (root / "Module" / "JbcUsbModule" / "JbcUsbModule.ino").read_text(encoding="utf-8")
master = (root / "OpenFumeExtractorMaster" / "src" / "MasterScheduler.cpp").read_text(encoding="utf-8")
registry = (root / "OpenFumeExtractorMaster" / "src" / "ModuleRegistry.h").read_text(encoding="utf-8")
web = (root / "OpenFumeExtractorMaster" / "src" / "WebStatus.inc.h").read_text(encoding="utf-8")

# The temporary raw 0xFA/D2 diagnostics were removed once the SOLD peripheral
# formats had been identified. Keep this migration guard so they cannot silently
# return and consume the OFE status payload again.
for token in ("raw_config[31]", "memcpy(next.raw_config,f.data,31)",
              "resp.payload[o++]=0xD2"):
    assert token not in module
assert "resp.payload[q] == 0xD2" not in master
assert "raw_config[31]" not in registry
for token in ("raw_config_hex", "jbcUsbSoldPeripheralRawText", "Raw 0xFA"):
    assert token not in web

# Semantic 31-byte peripheral decoding remains the supported representation.
assert "f.command == JBC_CMD_PERIPHERAL_CONFIG_SOLD" in module
assert "if(f.len!=31)" in module
assert "SoldPeripheralState& ps=jbc_sold_peripherals[id]" in module
assert "bool fae_device_id=true" in module
assert "next.type=6" in module

print("JBC SOLD semantic peripheral decoding and raw-diagnostics cleanup: OK")
