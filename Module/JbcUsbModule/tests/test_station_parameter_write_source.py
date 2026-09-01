from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / "JbcUsbModule.ino").read_text(encoding="utf-8")
BUS = (ROOT / "src" / "Rs485PeripheralBus.h").read_text(encoding="utf-8")


def test_config_actions_are_shared_with_master_protocol():
    assert "JBC_USB_CONFIG_SELECTED_TEMP = 0x02" in BUS
    assert "JBC_USB_CONFIG_SELECTED_FLOW = 0x03" in BUS
    assert "JBC_USB_CONFIG_LEVELS = 0x04" in BUS


def test_original_jbc_write_commands_are_present():
    block = SRC.split("static bool jbc_config_prepare", 1)[1].split(
        "static bool poll_jbc_config_write", 1
    )[0]
    assert "next.command[0] = 0x51" in block  # selected temperature
    assert "next.command[0] = 0x5A" in block  # HA selected flow
    assert "next.command[0] = 0x35" in block  # FE selected flow
    assert "next.command[0] = 0x34" in block  # SOLD P01/P02 levels
    assert "next.command[0] = 0x41" in block  # HA P02 levels


def test_write_is_control_wrapped_and_read_back():
    assert "JBC_CONFIG_WRITE_ENTER_CONTROL" in SRC
    assert "jbc_config_send_control_mode(true)" in SRC
    assert "jbc_config_send_control_mode(false)" in SRC
    assert "JBC_CONFIG_WRITE_VERIFY_READ" in SRC
    assert "jbc_config_send_verify()" in SRC


def test_p01_solder_levels_use_four_original_transactions():
    block = SRC.split("jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_01", 1)[1]
    assert "next.command_count = 4" in block
    assert "0x36U + i * 2U" in block
    assert "0xFFFFU" in block


def test_p02_level_switches_preserve_configured_values_when_disabled():
    block = SRC.split("jbc_station_kind == JBC_STATION_SOLD && jbc_frame_protocol == JBC_PROTO_02", 1)[1].split(
        "jbc_station_kind == JBC_STATION_HA && jbc_frame_protocol == JBC_PROTO_02", 1
    )[0]
    assert "next.data[0][1] = selected;" in block
    assert "put_u16_le(next.data[0] + o + 1, temp[i]);" in block
    assert "enabled ? selected : 0xFFU" not in block
    assert "? temp[i] : 0xFFFFU" not in block

    ha_block = SRC.split("jbc_station_kind == JBC_STATION_HA && jbc_frame_protocol == JBC_PROTO_02", 1)[1].split(
        "} else return false;", 1
    )[0]
    assert "next.data[0][1] = selected;" in ha_block


def test_unsupported_jtse_temperature_levels_are_rejected():
    block = SRC.split("next.action == JBC_USB_CONFIG_LEVELS", 1)[1].split(
        "jbc_config_write = next", 1
    )[0]
    assert "!ha_supports_temp_levels()" in block


def test_fe_flow_uses_original_dll_public_to_native_mapping():
    block = SRC.split("jbc_station_kind == JBC_STATION_FE", 1)[1].split(
        "next.verify_command", 1
    )[0]
    assert "450U" in block
    assert "130U" in block
    assert "1000U - minimum" in block
