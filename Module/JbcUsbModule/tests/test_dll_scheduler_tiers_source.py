from pathlib import Path

src = Path(__file__).resolve().parents[1] / "JbcUsbModule.ino"
text = src.read_text(encoding="utf-8")

assert "#define OFE_MODULE_FW_PATCH 75" in text

# SOLD/HA InfoPort change bits mirror the original DLL callbacks and only pull
# the affected slow tier forward.
assert "jbc_change_refresh_flags |= (uint8_t)(change_flags & 0x83U);" in text
assert "jbc_change_tool_port_mask |= (uint8_t)((change_flags >> 2) & 0x0FU);" in text
assert "next_sold_selected_temp_fast = true" in text
assert "next_sold_counter_fast = true" in text
assert "next_ha_selected_fast = true" in text
assert "next_ha_counter_fast = true" in text

# Original UpdateData cadence, distributed over the interval instead of queued
# as a burst: selected values 2 s, status 5 s, settings 15 s, counters 60 s.
assert "2000UL / max((uint8_t)1, limit)" in text  # SOLD selected temperature
assert "60000UL / ((uint32_t)stages * max((uint8_t)1, limit))" in text  # SOLD P02 counters
assert "next_sold_station_status_poll_ms = now + (ok ? 5000UL" in text
assert "next_sold_connect_poll_ms = now + (ok ? 15000UL" in text
assert "15000UL / (2UL * max((uint8_t)1, limit))" in text  # SOLD sleep/hiber settings

ha = text[text.index("// Original UpdateData_HA tiers:"):text.index("if (!uid_provisioning && (int32_t)(now - next_station_error_poll_ms)")]
assert "2000UL / (3UL * limit)" in ha
assert "1000UL / limit" in ha
assert "60000UL / (8UL * limit)" in ha
assert "next_ha_station_status_poll_ms = now + (ok ? 15000UL" in ha
assert "15000UL / ((uint32_t)stages * limit)" in ha

# The HA 15 s detail group must no longer contain the selected-value or counter
# commands; those have dedicated 2 s / 60 s schedulers now.
detail = ha[ha.index("static const uint8_t cmds_with_levels[] = {"):]
for cmd in (
    "JBC_CMD_SELECT_TEMP_HA", "JBC_CMD_SELECT_FLOW_HA", "JBC_CMD_SELECT_EXT_TEMP_HA",
    "JBC_CMD_ACTUAL_EXT_TEMP_HA", "JBC_CMD_COUNTER_PLUG_HA", "JBC_CMD_COUNTER_WORK_HA",
    "JBC_CMD_COUNTER_PLUG_PARTIAL_HA", "JBC_CMD_COUNTER_WORK_PARTIAL_HA",
):
    assert cmd not in detail

# StationError is the DLL's High (5 s) tier for normal station families.
assert "next_station_error_poll_ms = now + (jbc_send_station_error() ? 5000UL : 50UL);" in text

# The DLL runs every tier once immediately after connect before starting the
# periodic counters. Preserve that behavior without queue bursts.
assert "jbc_scheduler_prime_pending = true;" in text
assert "if (!uid_provisioning && jbc_scheduler_prime_pending)" in text
assert "next_sold_counter_fast = jbc_frame_protocol == JBC_PROTO_02" in text
assert "next_ha_counter_fast = true" in text

# v1.1.61 fidelity corrections recovered directly from UpdateData_SOLD/SF and
# the Moderate-tier UpdateMicros implementations.
assert 'next_sold_robot_status_poll_ms = now + (ok ? 5000UL : 150UL);' in text
assert 'next_sold_selected_profile_poll_ms = now + (ok ? spacing : 150UL);' in text
assert '5000UL / limit' in text
assert 'next_sold_micro_version_poll_ms = now + 15000UL;' in text
assert 'static uint32_t next_device_versions_poll_ms = 0;' in text
assert 'next_device_versions_poll_ms = 0;' in text
assert 'next_device_versions_poll_ms = now + (ok ? 15000UL : 250UL);' in text

# SF UpdatePortInfo contains Feeding only. DispenserMode (0x30) is one of four
# UpdateAllToolParam reads in the 15 s Moderate tier.
sf_port = text[text.index('if (!uid_provisioning && (int32_t)(now - next_port_poll_ms)'):text.index('if (!uid_provisioning && jbc_station_kind == JBC_STATION_CL')]
assert 'if (jbc_station_kind == JBC_STATION_SF)' in sf_port
assert 'next_port_poll_ms = now + 15000UL;' in sf_port
sf_tool = text[text.index('// DLL UpdateAllToolParam(): ToolEnabled, DispenserMode'):text.index('if (!uid_provisioning && jbc_station_kind == JBC_STATION_SF', text.index('// DLL UpdateAllToolParam(): ToolEnabled, DispenserMode') + 20)]
assert 'JBC_CMD_TOOL_ENABLED_SF, JBC_CMD_INFO_PORT' in sf_tool
assert 'JBC_CMD_SPEED_SF, JBC_CMD_LENGTH_SF' in sf_tool
assert 'next_sf_tool_param_fast ? 150UL : 3750UL' in sf_tool
