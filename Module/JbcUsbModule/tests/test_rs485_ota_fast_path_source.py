from pathlib import Path

SRC = (Path(__file__).resolve().parents[1] / "JbcUsbModule.ino").read_text()

assert "#define OFE_MODULE_FW_PATCH 75" in SRC
loop = SRC[SRC.index("void loop() {"):]

# RS485 OTA must NOT suppress USB/JBC servicing.  The 1.1.69 early-return
# fast path was reverted in 1.1.70 after the Master-side fast ACK retry fixed
# the visible update stalls without needing to pause the JBC bus.
first_rs485 = loop.index("poll_rs485();")
usb = loop.index("poll_usb_transport();", first_rs485)
jbc_rx = loop.index("poll_jbc_rx();", usb)
jbc_proto = loop.index("poll_jbc_protocol();", jbc_rx)
second_rs485 = loop.index("poll_rs485();", jbc_proto)
assert first_rs485 < usb < jbc_rx < jbc_proto < second_rs485

# There must be no OTA-only early return between first RS485 service and USB/JBC.
segment = loop[first_rs485:usb]
assert "if (fw_update_active)" not in segment
assert "return;" not in segment
