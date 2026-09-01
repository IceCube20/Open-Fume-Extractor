from pathlib import Path

src = (Path(__file__).resolve().parents[1] / "JbcUsbModule.ino").read_text()

assert "#define OFE_MODULE_FW_PATCH 75" in src
assert "JBC_CONTI_SPEED_OFF = 0" in src
assert "JBC_CONTI_SPEED_10MS = 1" in src
assert "jbc_station_kind == JBC_STATION_SOLD || jbc_station_kind == JBC_STATION_HA" in src
assert "const bool stream_off = jbc_continuous_valid" in src
assert "jbc_continuous_speed == JBC_CONTI_SPEED_OFF" in src
assert "jbc_send_conti_write(JBC_CONTI_SPEED_10MS, ports)" in src
assert "an already-active station-selected rate is left untouched" in src
assert "jbc_continuous_speed == JBC_CONTI_SPEED_OFF) return;" in src
assert "jbc_ports[port].sold_conti_valid = false;" in src
assert "Stability A/B profile: SOLD/DDE does not use M_I_CONTIMODE" not in src
print("SOLD continuous restored to original 10 ms startup behavior: OK")
