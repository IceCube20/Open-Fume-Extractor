# JbcUsbModule

OpenFumeExtractor peripheral for connecting JBC stations through the CP210x-based USB interface.

- **MCU:** ESP32-S3 with native USB in host mode
- **JBC USB bridge:** Silicon Labs CP210x
- **UART behind CP210x:** 500000 baud, 8E1, no flow control
- **JBC protocols:** Protocol 01 **and** Protocol 02 with automatic detection
- **Operation:** read-only station monitoring in this first version
- **OFE default address:** `0x11`

The module reports the station's own model string, reported command protocol, detected frame protocol, software/hardware versions and the number/state of up to four ports to the OpenFumeExtractor master. Known JBC models use the port-count table recovered from JBC Connect; unknown/new model strings remain visible and are probed instead of being rejected.

See [`JBC_USB_PROTOCOL.md`](JBC_USB_PROTOCOL.md) for the protocol/state-machine details and [`tests/test_protocol.py`](tests/test_protocol.py) for host-side codec vectors.

## Hardware notes

The ESP32-S3 must act as USB host, so the module board must provide protected/current-limited 5 V VBUS. With the internal USB PHY, the native USB data pins are GPIO19 (D-) and GPIO20 (D+). Use a separate programming/debug UART while validating USB-host operation.

The sketch uses the ESP-IDF USB Host API exposed by Arduino-ESP32 3.x. Hardware validation with the exact Arduino-ESP32/core version used for the final board is still required.


## Original JBC_Connect reference

For the protocol-fidelity mapping used by module 0.1.26, see [`JBC_CONNECT_REFERENCE.md`](JBC_CONNECT_REFERENCE.md).
