from pathlib import Path

src = (Path(__file__).resolve().parents[1] / 'JbcUsbModule.ino').read_text()

# Model/features recovered from CFeaturesDataInitializer.
assert '{"CFE",1,JBC_STATION_SOLD}' in src
assert '{"PHXL",1,JBC_STATION_PH}' in src
assert '(!strcmp(jbc_model, "CDE") || !strcmp(jbc_model, "CFE")) && jbc_model_version >= 7' in src
assert '!strcmp(jbc_model, "CDE") || !strcmp(jbc_model, "CFE")' in src

# Explicit ReadLockPort commands from SendFrame01/02_SOLD.
assert 'JBC_CMD_LOCK_PORT_P02 = 0x88' in src
assert 'JBC_CMD_LOCK_PORT_P01 = 0xD4' in src
assert 'enabled = f.data[0] == 0;' in src

# Continuous-mode stream and OFE continuous suction are different concepts.
assert 'JBC_CMD_CONTI_READ = 0x80' in src
assert 'JBC_CMD_CONTI_WRITE = 0x81' in src
assert 'JBC_CMD_CONTI_INFO = 0x82' in src
assert 'JBC_CONTI_SPEED_10MS = 1' in src
assert 'JBC_CONTI_SPEED_100MS = 4' in src
assert 'jbc_send_conti_read()' in src
# After the A/B test ruled out ContiMode as the trigger, SOLD and HA again use
# the original DLL startup behavior: if ReadContiMode reports OFF, start 10 ms.
assert 'const bool stream_off = jbc_continuous_valid' in src
assert 'jbc_send_conti_write(JBC_CONTI_SPEED_10MS, ports)' in src
assert 'an already-active station-selected rate is left untouched' in src
assert 'jbc_continuous_speed == JBC_CONTI_SPEED_OFF) return;' in src
assert 'decode_conti_info(f);' in src
assert 'ps.tool == 3 || ps.tool == 4' in src
assert 'sold_conti_last_ms' in src
assert 'conti_fresh' in src
assert 'Do not alias bit5 to QSTLock or Stand' in src
assert 'fast_flags |= FAST_FLAG_CONTINUOUS' not in src

print('JBC DLL-fidelity source invariants: OK')


# HA/JT/JTSE uses the same JBC continuous-mode transport, with a distinct
# 14-byte P02 live block and M_INF_PORT as the richer fallback/detail source.
assert 'jbc_station_kind != JBC_STATION_SOLD && jbc_station_kind != JBC_STATION_HA' in src
assert 'count * 14U' in src
assert 'const uint16_t flow = get_u16_le(f.data + base + 2);' in src
assert 'const uint16_t time_to_stop = get_u16_le(f.data + base + 10);' in src
assert 'ha_conti_fresh' in src
assert 'next.selected_flow_permille = prev.selected_flow_permille;' in src
assert 'next.ha_counter_suction_cycles = prev.ha_counter_suction_cycles;' in src
assert 'jbc_send_ha_connect_status()' in src
assert 'ha_flags |= 0x1000U' in src and 'ha_flags |= 0x2000U' in src
assert 'JBC_CMD_SELECT_TEMP_HA' in src and 'JBC_CMD_SELECT_FLOW_HA' in src

print('JBC HOT_AIR DLL-fidelity source invariants: OK')

# HA diagnostics/counters completion in 0.1.29.
for token in ('JBC_CMD_HEATER_STATUS_HA = 0x35','JBC_CMD_SUCTION_STATUS_HA = 0x37',
              'JBC_CMD_COUNTER_PLUG_PARTIAL_HA = 0xD0','JBC_CMD_COUNTER_SUCTION_CYCLES_PARTIAL_HA = 0xD6',
              'JBC_CMD_REMOTE_MODE_HA = 0x60','JBC_CMD_ROBOT_CONFIG_HA = 0xF0','JBC_CMD_ROBOT_STATUS_HA = 0xF2'):
    assert token in src
assert 'next.ha_diag_flags = prev.ha_diag_flags;' in src
assert 'resp.payload[o++] = 0xD9;' in src
assert 'const size_t ha_diag_need = 53U + profile_len;' in src
assert 'next_ha_telemetry_stage = 0' in src and 'next_ha_telemetry_stage = 1' in src
assert 'next_ha_telemetry_stage = 2' in src and 'resp.payload[o++]=0xDE' in src
print('JBC HOT_AIR diagnostic/partial-counter source invariants: OK')

# A station power-cycle may not disconnect the USB-powered CP210x. Every new
# P02 handshake must therefore reset the logical station session before FW/UID
# discovery, otherwise a previous UUID provisioning DONE state can suppress a
# new write when a volatile-ID station (JTSE) comes back with an empty ID.
hs = src.index('if (f.frame_protocol == JBC_PROTO_02 && f.command == JBC_CMD_HS &&')
hs_block = src[hs:hs + 1800]
assert 'const bool had_session = (fast_flags & FAST_FLAG_CONNECTED)' in hs_block
assert 'clear_jbc_runtime(had_session);' in hs_block
assert hs_block.index('clear_jbc_runtime(had_session);') < hs_block.index('jbc_frame_protocol = JBC_PROTO_02;')
assert 'provisioning path still writes exclusively after an actual invalid reply' in hs_block
print('JBC P02 station-reconnect / volatile Device-ID lifecycle invariant: OK')


# Volatile Device-ID startup/retry fidelity: a powered-cycle JTSE can handshake
# before its Device-ID storage is ready. Reads continue until a valid ID exists;
# writes are still gated by an actual invalid reply and delayed by a startup
# grace period. Failed verification returns to read/retry instead of becoming a
# terminal state, and one generated candidate is reused for the whole session.
assert 'JBC_UID_INITIAL_READ_DELAY_MS = 500UL' in src
assert 'JBC_UID_WRITE_GRACE_MS = 3000UL' in src
assert 'JBC_UID_RETRY_READ_MS = 1000UL' in src
assert 'JBC_UID_REPROVISION_DELAY_MS = 1500UL' in src
assert 'jbc_uid_write_not_before_ms = uid_now + JBC_UID_WRITE_GRACE_MS;' in src
assert 'const bool uid_valid = jbc_device_uid_is_valid(jbc_device_uid, jbc_device_uid_len);' in src
assert 'if (!uid_provisioning && !uid_valid && (int32_t)(now - next_uid_poll_ms) >= 0)' in src
assert 'jbc_device_uid_attempts >= 3' not in src
assert 'if (jbc_device_uid_is_valid(jbc_uid_generated, jbc_uid_generated_len)) return true;' in src
assert 'case JBC_UID_PROVISION_FAILED:' in src
failed = src.index('case JBC_UID_PROVISION_FAILED:')
failed_block = src[failed:failed + 700]
assert 'jbc_uid_provision_state = JBC_UID_PROVISION_IDLE;' in failed_block
assert 'next_uid_poll_ms = now;' in failed_block
invalid = src.index('} else if (jbc_uid_provision_state == JBC_UID_PROVISION_IDLE) {', src.index('static void decode_device_uid'))
invalid_block = src[invalid:invalid + 1000]
assert 'jbc_uid_write_not_before_ms' in invalid_block
assert 'jbc_begin_uid_provisioning();' in invalid_block
assert 'only an actual invalid reply enters this path' in invalid_block
print('JBC volatile Device-ID startup grace / indefinite read+verify retry invariant: OK')
