from pathlib import Path

src = (Path(__file__).resolve().parents[1] / 'JbcUsbModule.ino').read_text()

# HOT AIR: UpdateData_HA station parameters including PIN and beep.
for token in (
    'JBC_CMD_PIN_ENABLED_HA = 0xA8',
    'JBC_CMD_PIN_HA = 0xAC',
    'JBC_CMD_BEEP_HA = 0xB3',
    'JBC_CMD_REMOTE_MODE_HA = 0x60',
    'JBC_CMD_TEMP_UNIT_HA = 0xA0',
    'JBC_CMD_MAXMIN_TEMP_HA = 0xA2',
    'JBC_CMD_MAXMIN_FLOW_HA = 0xA4',
    'JBC_CMD_MAXMIN_EXT_TEMP_HA = 0xA6',
    'JBC_CMD_SELECTED_PROFILE_HA = 0x9A',
    'JBC_CMD_ROBOT_CONFIG_HA = 0xF0',
    'JBC_CMD_ROBOT_STATUS_HA = 0xF2',
):
    assert token in src, token
assert 'JBC_CMD_PIN_ENABLED_HA, JBC_CMD_PIN_HA, JBC_CMD_BEEP_HA' in src
assert 'resp.payload[o++]=0xDE' in src

# SOLD Protocol-01 UpdateData completion: station settings, per-tool settings,
# global counters and the distinct F0..FC partial-counter command family.
for token in (
    'JBC_CMD_FIX_TEMP_P01_SOLD = 0x31',
    'JBC_CMD_LEVEL1_P01_SOLD = 0x35',
    'JBC_CMD_LEVEL2_P01_SOLD = 0x37',
    'JBC_CMD_LEVEL3_P01_SOLD = 0x39',
    'JBC_CMD_DELAY_TIME_P01_SOLD = 0x59',
    'JBC_CMD_REMOTE_MODE_SOLD = 0x60',
    'JBC_CMD_STATUS_REMOTE_P01_SOLD = 0x62',
    'JBC_CMD_TEMP_UNIT_P01_SOLD = 0xA0',
    'JBC_CMD_N2_MODE_P01_SOLD = 0xA6',
    'JBC_CMD_HELP_TEXT_P01_SOLD = 0xA8',
    'JBC_CMD_POWER_LIMIT_SOLD = 0xAA',
    'JBC_CMD_PIN_SOLD = 0xAC',
    'JBC_CMD_BEEP_P01_SOLD = 0xB3',
    'JBC_CMD_PIN_ENABLED_P01_SOLD = 0xBD',
    'JBC_CMD_COUNTER_PLUG_PARTIAL_P01_SOLD = 0xF0',
    'JBC_CMD_COUNTER_DESOLD_CYCLES_PARTIAL_P01_SOLD = 0xFC',
):
    assert token in src, token
assert 'Protocol-01 uses A8 for HelpText and BD for PINEnabled' in src
# Regression: numeric A8 must NOT be remapped to BD inside the P01 sender.
p01_sender = src[src.index('static bool jbc_send_sold_station_read(uint8_t command) {'):src.index('static bool jbc_send_sold_peripheral_read(uint8_t command, uint8_t id) {')]
assert 'if (actual == JBC_CMD_PIN_ENABLED_SOLD) actual = JBC_CMD_PIN_ENABLED_P01_SOLD;' not in p01_sender
assert 'JBC_CMD_HELP_TEXT_P01_SOLD' in p01_sender and 'JBC_CMD_PIN_ENABLED_P01_SOLD' in p01_sender
assert 'JBC_CMD_TIP_TEMP_SOLD,JBC_CMD_POWER_PERTHOUSAND_SOLD' in src
assert 'JBC_CMD_DELAY_TIME_P01_SOLD,JBC_CMD_STATUS_REMOTE_P01_SOLD' in src
assert 'f.command == JBC_CMD_DELAY_TIME_P01_SOLD' in src
assert 'f.command == JBC_CMD_STATUS_REMOTE_P01_SOLD' in src

# SOLD Protocol-02 safe scalar/service reads beyond the normal UpdateData loop.
for token in (
    'JBC_CMD_INTERFACE_CONFIG_SOLD = 0xA0',
    'JBC_CMD_AUTOCLEAN_SOLD = 0xA6',
    'JBC_CMD_ASSISTANT_WARNING_SOLD = 0x8D',
    'JBC_CMD_SOLDERING_RESULT_SOLD = 0x8C',
    'JBC_CMD_STATION_DATETIME_SOLD = 0xBB',
    'JBC_CMD_FRONTAL_CONNECTION_SOLD = 0xE3',
    'JBC_CMD_TYPE_GROUND_P02_SOLD = 0xBA',
    'JBC_CMD_STATION_INTERFACE_P02_SOLD = 0xBE',
    'JBC_CMD_ETHERNET_P02_SOLD = 0xE7',
):
    assert token in src, token
for helper in ('sold_supports_excellence_289()', 'sold_supports_ground_type()', 'sold_supports_ethernet()'):
    assert helper in src
assert 'resp.payload[o++]=0xDD' in src
assert 'resp.payload[o++]=0xDC' in src
assert 'resp.payload[o++]=0xDF' in src

# Safety boundary: 0x87 on P02 is read+clear and must never enter an automatic
# diagnostic table. Reset/default 0xB0 is intentionally not implemented as a
# read-only command. File/profile transfer 0x90..0x97 is kept out of the cyclic
# status poll and belongs to a separate transfer API.
poll = src[src.index('static void poll_jbc_protocol'):src.index('static void rs485_get_state')]
assert 'JBC_CMD_ALARM_TRIGGER_NCLEAR_SOLD' not in poll
assert '0xB0' not in poll
for cmd in ('JBC_CMD_FILE_START', 'JBC_CMD_FILE_BLOCK', 'JBC_CMD_FILE_END'):
    assert cmd not in poll

print('JBC HA/SOLD safe READ-only coverage invariants: OK')

# ALE Tin Feeder is the last unique safe public SOLD Get... API. It must stay
# strictly feature-gated to ALE and use the DLL's 0x70/0x72 read commands.
assert 'JBC_CMD_ALE_FEEDER_INFO_SOLD = 0x70' in src
assert 'JBC_CMD_ALE_FEEDER_PROGRAM_SOLD = 0x72' in src
assert 'sold_supports_ale_feeder()' in src
assert '!strcmp(jbc_model, "ALE")' in src
assert 'jbc_send_sold_ale_feeder_read' in src
assert 'decode_sold_ale_feeder' in src
assert 'resp.payload[o++]=0xD5' in src
assert 'sold_feeder_program_length' in src and 'sold_feeder_program_speed' in src

# Slow read-only fields must survive the frequent M_INF_PORT rebuild, just like
# the previously fixed HA setpoint/detail state.
assert src.count('next.sold_readonly_port_flags = jbc_ports[port].sold_readonly_port_flags;') == 2
assert src.count('next.sold_feeder_flags = jbc_ports[port].sold_feeder_flags;') == 2

# Assistant decoding must happen only after the port state/reference exists.
dec = src[src.index('static void decode_sold_detail(const JbcFrame& f) {'):src.index('static void decode_info_port', src.index('static void decode_sold_detail(const JbcFrame& f) {')) if 'static void decode_info_port' in src[src.index('static void decode_sold_detail(const JbcFrame& f) {'):] else len(src)]
ps_pos = dec.index('JbcPortState& ps = jbc_ports[port];')
assert dec.index('ps.sold_assistant_warning_code', ps_pos) > ps_pos
assert dec.index('ps.sold_result_similarity', ps_pos) > ps_pos
print('JBC ALE/read-only persistence/decoder compile guards: OK')

# k26 grouped counters expose unique ALE/CDE data beyond the common counters.
for token in (
    'sold_special_counter_flags',
    'sold_tin_deliver_cycles', 'sold_partial_tin_deliver_cycles',
    'sold_cde_sold_number', 'sold_cde_partial_sold_number',
    'resp.payload[o++]=0xD4',
):
    assert token in src, token
assert 'next.sold_special_counter_flags = jbc_ports[port].sold_special_counter_flags;' in src
# ALE M_INF_PORT carries feeder motor ON + direction in byte 11.
assert 'next.sold_feeder_flags |= 0x0040U;' in src
assert 'next.sold_feeder_motor_on = (f.data[11] & 0x01U) ? 1 : 0;' in src
assert 'next.sold_feeder_motor_direction = (f.data[11] & 0x02U) ? 1 : 0;' in src

# SOLD/P02 UpdateMicros parity: 0x21 probes of 0x7F and 0x00 recur in the
# DLL Moderate (~15 s) tier. Source-0 IMX refreshes the secondary SW string; a
# station-address reply refreshes the main protocol/model/SW/HW without redoing
# discovery or UUID provisioning.
assert 'jbc_send_sold_micro_version' in src
assert 'next_sold_micro_version_stage == 0 ? 0x7FU : 0x00U' in src
assert 'next_sold_micro_version_poll_ms = now + 15000UL;' in src
assert 'f.source == 0' in src and 'parse_secondary_firmware_string' in src
active_fw = src[src.index('if (f.command == JBC_CMD_FIRMWARE) {'):src.index('if (jbc_link_state != JBC_LINK_ACTIVE) return;')]
assert 'if (jbc_link_state == JBC_LINK_ACTIVE)' in active_fw
assert 'parse_secondary_firmware_string(f.data, f.len);' in active_fw
assert 'else if (f.source == jbc_station_addr)' in active_fw
assert 'parse_firmware_string(f.data, f.len);' in active_fw
assert 'jbc_scheduler_prime_pending = true;' not in active_fw[:active_fw.index('return;\n    }') + 1]
print('JBC k26 extended counters / recurring micro-version invariants: OK')

# Final audit boundary: P01 ReadCurrent(0x53) exists in SendFrame01_SOLD, but
# ReceiveFrame01_SOLD has no command-83 decoder. The OFE P01 service round-robin
# must document this and must not invent an observable current value.
p01_block = src[src.index('// Protocol-01 UpdateData_SOLD completion plus safe observable service reads.'):src.index('// P01 global/partial counters')]
assert 'ReceiveFrame01_SOLD has no command-83 decoder' in p01_block
assert 'JBC_CMD_CURRENT_SOLD' not in p01_block

# Maximum telemetry prefix is 99 bytes (32-byte Device ID included). Every
# extension stage must still fit the OFE 192-byte RS485 MAX_PAYLOAD.
for need in (65, 24, 45, 53, 17, 40, 55, 79, 61, 57):
    assert 99 + need <= 192
assert 'const size_t need=79U;' in src  # ALE is the largest extension.
print('JBC read-only safety-boundary / RS485 payload invariants: OK')
