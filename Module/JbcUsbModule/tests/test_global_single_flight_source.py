from pathlib import Path
src=(Path(__file__).resolve().parents[1]/"JbcUsbModule.ino").read_text()

assert "JBC_SINGLE_FLIGHT_TIMEOUT_MS = 500UL" in src
start=src.index("static bool jbc_single_flight_ready")
end=src.index("// JBC_Connect queues requests", start)
gate=src[start:end]
assert "JBC_STATION_SOLD" not in gate
assert "JBC_STATION_HA" not in gate
assert "JBC_STATION_CL" not in gate
assert "JBC_STATION_PH" not in gate
assert "JBC_STATION_FE" not in gate
assert "JBC_STATION_SF" not in gate
assert "frame_protocol == JBC_PROTO_02" in gate
assert "frame_protocol == JBC_PROTO_01" in gate
assert "pending_by_fid" in gate
assert "p01_pending" in gate

send_start=src.index("static bool jbc_send_frame(")
send_end=src.index("static bool jbc_send_handshake_p02", send_start)
send=src[send_start:send_end]
assert "if (!jbc_single_flight_ready(frame_protocol, target, response, wait_response)) return false;" in send
assert "if (ok && !response && wait_response && ((target & 0x0FU) != 0x0FU))" in send
assert "pending_by_fid[fid].command = command;" in send
assert "!wait_response" in gate
assert "((target & 0x0FU) == 0x0FU)" in gate
assert "const bool wait_response = target != 0;" in src

# Continuous telemetry is unsolicited RX and must remain independent of the
# response-waiting request slot. SOLD/HA are restored to T_10mS startup.
assert "jbc_send_conti_write(JBC_CONTI_SPEED_10MS, ports)" in src
assert "f.command == JBC_CMD_CONTI_INFO" in src

# All six station families send through the common frame function; no family
# gets a private burst-capable P02 transport path.
for token in ("jbc_send_sold_station_read", "jbc_send_ha_station_diag",
              "jbc_send_cl_read", "jbc_send_ph_read",
              "jbc_send_fe_read", "jbc_send_sf_read"):
    assert token in src, token
assert "sold_p02_single_flight_ready" not in src
print("JBC global P01/P02 single-flight transport invariants: OK")
