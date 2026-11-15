# P4 V3 GUI Test: Reboot Analysis

Checked on 2026-10-23 against the owner's Downloads test sketch. No device was
flashed. Initially only the shared LED correction was propagated to the main
project. After the owner confirmed reboot-free operation, the GUI ownership
model was also integrated into the maintained display ports; see
[Display GUI Task](Display-GUI-Task.md). The Downloads test sketch remains intact.

## Measurements Supplied By The Owner

- Repeated OFE scroll benchmark: 41.2-41.5 LVGL refreshes/s, 9.63-9.70 MPix/s,
  7.1 tiles/frame, 48-line draw buffer, approximately 22 ms average handler time.
- Pixel clock 24 MHz, theoretical scan 56.7 Hz, reported benchmark CPU 51%.
- Renderer is the original PPA fill-only mode; no reported PPA operation errors.
- The new GUI task runs on core 1 at priority 2 and owns the existing UI service
  block. The original loop still services status LEDs and the serial CLI.
- The FPS increase is therefore not evidence of a new PPA drawing feature.
  Scheduling and pixel clock differ from earlier experiments.

## Matching Crash Evidence

The log contains four identical assertions:

```text
assert failed: vTaskPriorityDisinheritAfterTimeout tasks.c:5270
(pxTCB != pxCurrentTCBs[ xPortGetCoreID() ])
```

The exact original ELF was still present in the owner's Arduino build cache.
Its SHA-256 matches the logged prefix `fa9206b9d`:

```text
fa9206b9dc03c0e116b26eb39ea63f894e0b056531490b11974c56a06d9cb812
```

Relevant decoded stack addresses:

| Address | Location |
| --- | --- |
| `0x4ff0a5e4` | `xQueueSemaphoreTake`, FreeRTOS `queue.c:1842` |
| `0x400522ec` | `espShow`, installed Adafruit NeoPixel `esp.c:52` |
| `0x4005219e` | `Adafruit_NeoPixel::show()` |
| `0x40045e28` | `loop()`, V3 sketch, status-LED tick |
| `0x400d129a` | Arduino `loopTask()` |

These are addresses recovered from stack memory, not a complete unwound
backtrace. They identify the failing LED/mutex path; they do not prove the
absence of unrelated bugs. The earlier WiFi-MAC startup warning is not the
assertion shown here.

## Failure Path And Correction

The P4 defaults to `OFE_STATUS_LED_ENABLE=1` with `OFE_STATUS_LED_PIN=-1`.
`OfeStatusLed::begin()` returns for that pin, but the original `tick()` could
still call `pixels_.show()`. In the installed NeoPixel `esp.c`, `espShow()` takes
a global mutex, then returns without giving it back if `rmtInit()` fails.
Subsequent calls can wait on the retained mutex (50 ms timeout) and enter the
assertion path above.

The shared header now compiles the NeoPixel include, object and hardware calls
only if all three conditions hold:

```cpp
OFE_STATUS_LED_ENABLE && OFE_STATUS_LED_PIN >= 0 && OFE_STATUS_LED_COUNT > 0
```

The change was applied to the owner's Downloads test header and to the canonical
Master header, then synchronized to all 11 module copies, including all four
display ports. The P4 generator retains the shared source without a P4-only
override. Configured physical LEDs retain their original implementation; logical
LED state, web indicators and ECO state remain available without physical LEDs.
No installed NeoPixel/LVGL library was edited. No LCD timing, UI asset/style or
V3 task priority/cadence was changed in the test sketch.

The S3 RGB display also defaults to enabled status LEDs with pin -1 and was
therefore exposed to the same call path. The small QSPI display defaults to a
physical GPIO46; ST7796 disables status LEDs by default. The guard is included
in all of them, but it is not a claim that each display had this particular
failure or will receive the same performance improvement.

## Verification And Remaining Tests

- Native test of the original cached header confirms `show()` is called without
  `begin()` when the pin is -1. This reproduces the invalid call path, not a
  hardware RMT failure or a FreeRTOS panic on the host.
- Fixed native tests pass for missing pin, disabled LEDs, zero LEDs and a valid
  configured LED. The first three never construct/call NeoPixel. A valid LED
  still renders and skips identical color updates.
- Shared-source check passes for Master and every module, including unchanged
  OFE bus headers and implementation.
- Source regression checks cover the P4 generator and the shared LED guards.
- Full V3 Arduino compile/link PASS in the independent build folder
  `C:/esp/ofe-p4-3.3.12/arduino-v3-led-fix-check`; log
  `tools/tests/build-checks/p4-v3-led-fix.log`. Program storage 1,990,492 bytes
  (59%), static globals 80,384 bytes. No device upload.
- Application BIN: 1,990,720 bytes, SHA-256
  `5522bed55e51f52b9d1b8b5bedd5757cb4750f6fc6c9359ded05e4cfd3dc0aec`.
- `check_p4_v3_led_fix.py` PASS: NeoPixel functions are absent from the ELF;
  V3 GUI task and PPA fill remain linked. BLE/RNG source hashes still match the
  pinned project manifest. Bootloader and partitions match the reference build.
- The owner's IDE rebuilt its original cache during verification, replacing the
  captured crash ELF. That new ELF was not used to decode the old crash. The
  earlier source diff recorded the main-sketch Git blob prefix `fd0bd20` and
  original LED-header SHA-256
  `1eec96af860cfe00732c0ccf4ea666b349542aaecfaee613c3ef28ca79136fd0`.
  The validator uses those recorded values as additional checks.
- The test sketch was also changed during the work to
  `OFE_STATUS_LED_ENABLE=0`. This concurrent change was preserved and is present
  in the full firmware build. Restoring that one default in memory reproduces
  the reviewed main-sketch blob; no GUI, timing or style edits were found. The
  native tests separately exercise enabled LEDs with pin -1, so the shared
  hardware guard is tested independently of that additional disable switch.
- P4 source/profile checks: 47 PASS. Additional combined P4, Fan/IO and Master
  hardening source checks: 38 PASS (the sets overlap).
- The owner subsequently confirmed that the test display no longer crashes.
  Long-term validation of the newly integrated ports is separate; do not claim
  a measured post-fix FPS from a compiler check.

For a controlled scheduling comparison, the existing V3 test already exposes
`DISPLAY_V3_GUI_TASK`: compare 0 and 1 with the corrected LED header and identical
PCLK, renderer, benchmark scene, radio and bus load. Both paths run the same UI
service block. A dedicated task is not a guarantee of 41 FPS on an S3: RGB
scanout, rendering and QSPI/SPI transfers have different limits.

Before enabling a GUI task by default on another port, ensure a single owner for
all LVGL drawing, callbacks, UI mutations and allocator inspection. Verify Home
updates, touch/scrolling, settings/keyboards, screensaver/backlight, BLE pairing,
RS485/WiFi and OTA, plus internal heap and task-stack headroom. Thread creation
failure must retain a working original-loop fallback.
This follows LVGL's documented
[Gateway Thread approach](https://lvgl.io/docs/open/9.4/details/integration/overview/threading#method-1-use-a-gateway-thread).
Splitting only `lv_timer_handler()` into another task while leaving widget writes
in `loop()` would violate that ownership model and is not the intended port.
