from pathlib import Path

SRC = (Path(__file__).resolve().parents[1] / "JbcUsbModule.ino").read_text()


def test_usb_patch_63():
    assert "#define OFE_MODULE_FW_PATCH 75" in SRC


def test_exhausted_single_flight_request_does_not_rediscover():
    start = SRC.index("static bool jbc_single_flight_ready")
    end = SRC.index("static uint8_t p02_recent_pending_count", start)
    block = SRC[start:end]
    assert "jbc_retry_timed_out_request(JBC_PROTO_02)" in block
    assert "jbc_retry_timed_out_request(JBC_PROTO_01)" in block
    assert "restart_jbc_discovery" not in block
    assert "continue;" in block
    assert "return true;" in block


def test_excessive_nacks_do_not_force_physical_rediscovery():
    start = SRC.index("if (f.command == JBC_CMD_NACK)")
    end = SRC.index("if (is_device_uid_frame(f))", start)
    block = SRC[start:end]
    assert "jbc_note_nack_and_check_excessive()" in block
    assert "jbc_clear_in_progress(f.frame_protocol)" in block
    assert "restart_jbc_discovery" not in block


def test_real_link_loss_still_rediscoveries():
    assert "last_jbc_frame_ms && (uint32_t)(now - last_jbc_frame_ms) > 4500UL" in SRC
    assert "restart_jbc_discovery(true, true); return;" in SRC
