from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
SRC = (ROOT / "Module/JbcUsbModule/JbcUsbModule.ino").read_text(encoding="utf-8")
WEB = ROOT / "OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/src"


def test_service_wire_action_is_shared():
    for path in (WEB / "bus/Rs485PeripheralBus.h", ROOT / "Module/JbcUsbModule/src/Rs485PeripheralBus.h"):
        assert "JBC_USB_CONFIG_SERVICE_READ = 0x05" in path.read_text()


def test_service_allowlist_never_contains_actions_or_datetime():
    block = SRC.split("static bool jbc_sold_service_read_supported(uint8_t command) {", 1)[1].split("static bool poll_jbc_service_read()", 1)[0]
    commands = re.findall(r"case (JBC_CMD_\w+):", block)
    assert set(commands) == {"JBC_CMD_REMOTE_MODE_SOLD", "JBC_CMD_POWER_LIMIT_SOLD", "JBC_CMD_INTERFACE_CONFIG_SOLD",
                             "JBC_CMD_AUTOCLEAN_SOLD", "JBC_CMD_FRONTAL_CONNECTION_SOLD", "JBC_CMD_TYPE_GROUND_P02_SOLD",
                             "JBC_CMD_STATION_INTERFACE_P02_SOLD"}
    assert "sold_supports_excellence_289()" in block
    assert "sold_supports_ground_type()" in block
    poll = SRC.split("static bool poll_jbc_service_read() {", 1)[1].split("static bool jbc_send_sold_peripheral_read", 1)[0]
    assert "3000UL" in poll and "fw_update_active" in poll
    assert "jbc_send_sold_station_read(jbc_service_read_pending)" in poll
    assert "jbc_send_conti_write" not in poll and "jbc_config_send_control_mode" not in poll


def test_service_http_is_developer_only_and_validated():
    handler = (WEB / "WebControls.inc.h").read_text(encoding="utf-8")
    block = handler.split('if (action == "service_read") {', 1)[1].split('const uint8_t port =', 1)[0]
    assert "!developer_mode_enabled" in block and "web.send(403" in block
    assert "!raw.length()" in block and "*end" in block and "!allowed" in block
    assert "JBC_USB_CONFIG_SERVICE_READ" in block
    assert "web.send(202" in block and "not yet confirmed" in block
    ui = (WEB / "WebStatus.inc.h").read_text(encoding="utf-8")
    assert 'if(!devMode||jbcUsbServicePending.has(addr))return;' in ui
    assert 'action:\'service_read\'' in ui


def test_no_tool_only_skips_tool_bound_detail_stages():
    assert "ok = !jbc_ports[p].tool && jbc_ha_detail_requires_tool(cmd);" in SRC
    assert "if (!ok) ok = jbc_send_ha_detail(p, cmd);" in SRC
    assert "if (!ok && (p >= JBC_MAX_PORTS || !jbc_ports[p].valid || !jbc_ports[p].tool))" not in SRC
