from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT / "JbcUsbModule.ino").read_text(encoding="utf-8")
BUS = (ROOT / "src" / "Rs485PeripheralBus.h").read_text(encoding="utf-8")


def test_station_family_write_commands_match_jbc_connect():
    block = SRC.split("static uint8_t jbc_station_name_write_command()", 1)[1].split(
        "static bool jbc_station_name_valid", 1
    )[0]
    assert "JBC_STATION_SOLD ? 0xB2" in block
    assert "case JBC_STATION_HA:" in block
    assert "case JBC_STATION_PH: return 0xB2" in block
    assert "case JBC_STATION_FE: return 0x5C" in block
    assert "case JBC_STATION_CL: return 0x55" in block


def test_station_name_write_is_queued_and_read_back():
    assert "jbc_station_name_write_queued = true" in SRC
    assert "poll_jbc_station_name_write()" in SRC
    assert "JBC_NAME_WRITE_VERIFY_READ" in SRC
    assert "JBC_NAME_WRITE_WAIT_VERIFY" in SRC
    assert "case CMD_JBC_USB_CONFIG: rs485_jbc_usb_config(req); break;" in SRC


def test_station_name_write_temporarily_uses_control_mode():
    enter = SRC.index("case JBC_NAME_WRITE_ENTER_CONTROL:")
    send = SRC.index("case JBC_NAME_WRITE_SEND_NAME:", enter)
    leave = SRC.index("case JBC_NAME_WRITE_LEAVE_CONTROL:", send)
    verify = SRC.index("case JBC_NAME_WRITE_VERIFY_READ:", leave)
    assert enter < send < leave < verify
    assert "jbc_station_name_send_control_mode(true)" in SRC[enter:send]
    assert "jbc_station_name_send_control_mode(false)" in SRC[leave:verify]
    assert "(uint8_t)':', control ? (uint8_t)'C' : (uint8_t)'M'" in SRC


def test_station_name_wire_command_is_shared():
    assert "CMD_JBC_USB_CONFIG = 0x38" in BUS
    assert "JBC_USB_CONFIG_STATION_NAME = 0x01" in BUS
