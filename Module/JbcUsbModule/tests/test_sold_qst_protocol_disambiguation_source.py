from pathlib import Path

src = Path(__file__).resolve().parents[1] / "JbcUsbModule.ino"
text = src.read_text(encoding="utf-8")

assert "if (f.frame_protocol == JBC_PROTO_02)" in text
assert "return f.command == JBC_CMD_QST_ACTIVATE_P02 || f.command == JBC_CMD_QST_STATUS_P02;" in text
assert "if (f.frame_protocol == JBC_PROTO_01)" in text
assert "return f.command == JBC_CMD_QST_ACTIVATE_P01 || f.command == JBC_CMD_QST_STATUS_P01;" in text
# P02 D0/D2 must still reach the SOLD detail decoder, which expects the
# original-DLL 5-byte partial counter payload.
assert "case JBC_CMD_COUNTER_PLUG_PARTIAL_SOLD:" in text
assert "case JBC_CMD_COUNTER_WORK_PARTIAL_SOLD:" in text
assert "if (f.len != 5)" in text
assert "#define OFE_MODULE_FW_PATCH 75" in text
