from pathlib import Path

src = (Path(__file__).resolve().parents[1] / "JbcUsbModule.ino").read_text(encoding="utf-8")

assert "JBC_CONTI_SPEED_10MS = 1" in src
assert "JBC_CONTI_SPEED_100MS = 4" in src

# The temporary SOLD=100 ms override is obsolete. SOLD and HOT_AIR now follow
# the original JBC behavior: start 10 ms telemetry only after an OFF read-back
# and preserve any rate already selected by the station.
assert "jbc_station_kind == JBC_STATION_SOLD || jbc_station_kind == JBC_STATION_HA" in src
assert "const bool stream_off = jbc_continuous_valid" in src
assert "jbc_continuous_speed == JBC_CONTI_SPEED_OFF" in src
assert "jbc_send_conti_write(JBC_CONTI_SPEED_10MS, ports)" in src
assert "an already-active station-selected rate is left untouched" in src
assert "? JBC_CONTI_SPEED_100MS : JBC_CONTI_SPEED_10MS" not in src
assert "sold_rate_mismatch" not in src
assert "jbc_send_conti_write(desired_speed, ports)" not in src

print("SOLD temporary 100 ms override remains removed: OK")
