from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
module = (ROOT / "Module/JbcUsbModule/JbcUsbModule.ino").read_text(encoding="utf-8")
master = (ROOT / "OpenFumeExtractorMaster/src/MasterScheduler.cpp").read_text(encoding="utf-8")
registry = (ROOT / "OpenFumeExtractorMaster/src/ModuleRegistry.h").read_text(encoding="utf-8")
web = (ROOT / "OpenFumeExtractorMaster/src/WebStatus.inc.h").read_text(encoding="utf-8")

# Hardware-confirmed DDE FAE peripheral special format:
# 24 ASCII hex chars (FAE Device-ID) + six spaces + peripheral id.
assert "bool fae_device_id=true" in module
assert "for(uint8_t bi=0;bi<24;++bi)if(!hex_ascii(f.data[bi]))" in module
assert "for(uint8_t bi=24;bi<30;++bi)if(f.data[bi]!=' ')" in module
assert "next.type=6" in module
assert "5:'MV',6:'FAE'" in web

# Temporary raw 0xFA diagnostics are removed after hardware identification.
assert "raw_config[31]" not in module
assert "raw_config[31]" not in registry
assert "Raw 0xFA" not in web
assert "raw_config_hex" not in web
assert "resp.payload[o++]=0xD2" not in module
assert "resp.payload[q] == 0xD2" not in master

# Captured hardware examples: normal MS config does not match FAE shape;
# FAE config is exactly 24 ASCII hex chars + six spaces + peripheral id.
def is_fae(raw: bytes) -> bool:
    return len(raw) == 31 and all(chr(b) in "0123456789ABCDEFabcdef" for b in raw[:24]) and raw[24:30] == b"      "

ms = bytes.fromhex("30 30 34 39 35 38 32 30 32 34 30 34 31 36 31 39 30 31 35 34 4D 53 30 31 20 20 20 20 20 20 00")
fae = bytes.fromhex("38 34 34 39 35 38 32 30 32 36 30 37 32 31 31 39 34 35 32 38 46 32 38 33 20 20 20 20 20 20 01")
assert not is_fae(ms)
assert is_fae(fae)
assert fae[:24].decode() == "84495820260721194528F283"
