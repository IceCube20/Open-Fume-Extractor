from pathlib import Path

src = Path(__file__).resolve().parents[1] / "JbcUsbModule.ino"
text = src.read_text(encoding="utf-8")

assert "#define OFE_MODULE_FW_PATCH 75" in text

# v1.1.59's SOLD-only startup counter tracker is intentionally gone. The DLL
# retries the individual in-progress message for every station family instead.
for stale in ("sold_initial_counter_tracking", "sold_initial_counter_verify_pending",
              "sold_initial_counter_seen[JBC_MAX_PORTS]"):
    assert stale not in text

assert "JBC_SINGLE_FLIGHT_TIMEOUT_MS = 500UL" in text
assert "JBC_MESSAGE_RETRY_COUNT = 4" in text
assert "jbc_retry_timed_out_request(JBC_PROTO_01)" in text
assert "jbc_retry_timed_out_request(JBC_PROTO_02)" in text
sf_start = text.index("static bool jbc_single_flight_ready")
sf_end = text.index("static uint8_t p02_recent_pending_count", sf_start)
assert "restart_jbc_discovery" not in text[sf_start:sf_end]

# Every family still receives an immediate initial tier snapshot. Once the
# retry layer succeeds, normal counter cadence returns to ~60 seconds.
for family in ("SOLD", "HA", "PH", "FE", "SF", "CL"):
    assert f"jbc_station_kind == JBC_STATION_{family}" in text
assert "next_sold_counter_fast = jbc_frame_protocol == JBC_PROTO_02" in text
assert "next_ha_counter_fast = true" in text
assert "next_ph_counter_fast = true" in text
assert "next_fe_counter_fast = true" in text
assert "next_sf_counter_fast = true" in text
assert "next_cl_counter_fast = true" in text
assert "60000UL" in text

print("JBC all-family initial snapshot / central DLL retry invariants: OK")
