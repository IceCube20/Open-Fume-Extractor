from pathlib import Path
src=(Path(__file__).resolve().parents[1]/'JbcUsbModule.ino').read_text()

# Public service reads remain decodable, but they are no longer injected into
# the recurring SOLD/P02 background traffic after the hardware trace showed
# ReadStationDateTime (0xBB) immediately before the DDE stopped responding.
poll=src[src.index('static uint32_t next_sold_station_param_poll_ms = 0;'):src.index('// ALE exposes two additional safe read APIs:')]
normal=poll[poll.index('static const uint8_t p02_cmds[]'):poll.index('// Public SOLD/P02 service reads')]
for token in ('JBC_CMD_INTERFACE_CONFIG_SOLD','JBC_CMD_AUTOCLEAN_SOLD','JBC_CMD_STATION_DATETIME_SOLD','JBC_CMD_FRONTAL_CONNECTION_SOLD','JBC_CMD_TYPE_GROUND_P02_SOLD','JBC_CMD_STATION_INTERFACE_P02_SOLD','JBC_CMD_REMOTE_MODE_SOLD','JBC_CMD_POWER_LIMIT_SOLD'):
    assert token not in normal, token
assert 'static const uint8_t service_cmds[]' not in poll
assert 'next_sold_service_snapshot_ms' not in src
assert 'Public SOLD/P02 service reads' in poll
assert 'never inject' in poll

# The DDE fix was generalized to the whole local JBC bus because the original
# Station_Com/QueueMessages transport invariant is station-class independent.
assert 'JBC_SINGLE_FLIGHT_TIMEOUT_MS = 500UL' in src
assert 'jbc_single_flight_ready(JbcProtocol frame_protocol, uint8_t target' in src
assert 'if (!jbc_single_flight_ready(frame_protocol, target, response, wait_response)) return false;' in src
# The gate must not contain a SOLD-only station-kind condition.
gate=src[src.index('static bool jbc_single_flight_ready'):src.index('// JBC_Connect queues requests', src.index('static bool jbc_single_flight_ready'))]
assert 'JBC_STATION_SOLD' not in gate
assert 'frame_protocol == JBC_PROTO_02' in gate
assert 'frame_protocol == JBC_PROTO_01' in gate

# NACK ends the FID transaction instead of leaving a stale pending entry.
hs=src.rindex('static void handle_jbc_frame(const JbcFrame& f) {')
handle=src[hs:src.index('static bool frame_matches_protocol', hs)]
assert 'if (f.command == JBC_CMD_NACK)' in handle
assert 'pending_by_fid[f.fid] = PendingRequest();' in handle

# Match the original receiver: invalid DateTime values are ignored if one is
# ever read explicitly/on demand later.
assert 'static bool sold_datetime_valid' in src
assert 'if(!sold_datetime_valid(f.data,f.len)) return;' in src

# DLL fidelity: every peripheral ID returned by 0xF9 is status-polled, even if
# its 0xFA configuration decodes to NO_TYPE/NO_PORT. Configuration/count and
# status have independent cadences (5 s and 2 s per ID round respectively).
assert 'jbc_send_sold_peripheral_read(JBC_CMD_PERIPHERAL_STATUS_SOLD,id)' in poll
assert 'next_sold_peripheral_status_poll_ms' in poll
assert 'next_sold_peripheral_config_poll_ms' in poll
assert 'jbc_send_sold_station_read(JBC_CMD_PERIPHERAL_COUNT_SOLD)' in poll
assert 'jbc_send_sold_peripheral_read(JBC_CMD_PERIPHERAL_CONFIG_SOLD,id)' in poll
assert 'semantic_config' not in poll
assert 'memcpy(next.hash_mcu_uid,f.data+2,4)' in src
assert 'memcpy(next.datetime,f.data+6,14)' in src
assert 'resp.payload[o++]=0xD3' in src
print('JBC DDE stability / single-flight / service-poll invariants: OK')
