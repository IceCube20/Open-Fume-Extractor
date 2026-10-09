# Open Fume Extractor 2.0.30 RC

This release contains the current community master and firmware for all ten
module/display targets. The community master does not enable developer mode;
private developer configuration and signing keys are not included.

## Changes since 2.0.12 RC

- Improved normal-polling latency, request scheduling and timeout diagnostics.
- Normal diagnostic timeouts are separated from firmware-transfer retries and
  the target's bounded restart window, for both RS485 and WLAN updates.
- A successful module update starts a recovery window of up to 15 seconds,
  ending earlier after a validated INFO response. Other modules retain normal
  timeout and offline handling during the update.
- Raw CRC, framing, length and sequence diagnostics remain visible; firmware
  updates do not globally disable transport-error detection.
- OFE physical responses use one buffered UART write per escaped frame. This
  does not change the wire protocol or guarantee elimination of electrical faults.
- Fan/IO routing and validation support configured channels beyond IN1/IN2.
- Continuous mode remains a runtime setting and is not restored after reboot.
- Weller filter data is treated as unavailable when the local device link is
  absent, rather than presenting stale values as a current filter warning.
- Display brightness changes while the screensaver is active retain the saved
  normal brightness but cap effective brightness at 30%. Waking restores the
  latest normal brightness without changing the screensaver timeout.

## Installation and updates

Use the signed normal `.bin` files for OTA. Use `*-merged.bin` only for initial
USB installation at offset `0x0`. Select the exact display target: the AXS15231B
320x480, ST7796 320x480 and RGB 800x480 images are not interchangeable.

All firmware files are attached individually to this GitHub release and indexed
in [Firmware Files](https://github.com/IceCube20/Open-Fume-Extractor/blob/v2.0.30RC/docs/FIRMWARE.md). The [online USB flasher](https://icecube20.github.io/Open-Fume-Extractor/)
uses the same merged images.

## Verification and remaining limitations

Master builds and source/native regression tests passed. The user-tested system
showed no normal RS485 module timeouts in its most recent diagnostic sample.
This is not a guarantee of error-free operation under all wiring and loads.

A rare malformed Fan/IO response remains under investigation. The improved
diagnostic counters must not be interpreted as a proven fix for that transport
problem. WLAN jitter can still cause occasional normal display timeouts.
