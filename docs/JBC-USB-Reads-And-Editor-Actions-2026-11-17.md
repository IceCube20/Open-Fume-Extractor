# JBC USB reads and profile editor actions

Versions: MCU-S3 / MCU-P4 2.1.23RC-Developer, JBC USB module 1.1.85RC.

## JBC USB

- CLM/CLMU ConnectStatus now follows the reference's complete, trimmed mode
  field. A letter C in a prefix or a multi-character value no longer means
  CONTROL. The optional first colon separates the prefix from the mode.
- The HA detail scheduler skips only tool-bound settings when no tool is
  connected. Profile mode and other port-level diagnostics remain reachable.
  Transport-busy requests retain their current stage for a later retry.
- Developer mode exposes a manual SOLD/P02 service selector in the station
  data section. It uses the existing authenticated, CSRF-protected
  `/jbc-usb/config` POST endpoint with `action=service_read` and a decimal
  command number.
- Wire request: CMD_JBC_USB_CONFIG (0x38), payload `{0x05, jbc_command}`.
  Legacy modules reject the new action; existing setting actions are unchanged.
- Allowlist: RemoteMode 0x60, PowerLimit 0xAA, Excellence interface 0xA0,
  AutoClean 0xA6, frontal connection 0xE3, ground type 0xBA, ALE interface 0xBE.
  The module applies model and protocol gates independently of the UI.
- Reads are one-shot, use the existing single-flight/retry transport, and do
  not enter CONTROL or write station configuration. One queued read at a time,
  a two-second acceptance interval and a three-second queue expiry bound work.
- OTA, disconnected links, UUID provisioning and configuration/name writes
  reject new reads. Disconnect/session reset clears pending requests.
- HTTP 202 means queued, NOT a confirmed station response. Existing telemetry
  publishes values only when the station reply is decoded. There is no new
  background service polling and no claim that cached values are fresh replies.
- StationDateTime 0xBB stays blocked due to the earlier DDE observation.
  Read-and-clear 0x87, reset/filter-reset/error-reset, files and arbitrary raw
  commands are not available through this endpoint.

## Profile editor layout

The Universal RS232 builder, Modbus builder and expert profile action rows
share responsive wrapping in WebFormStyles.h. Export and USB export receive
room as a group; narrower fields wrap without forcing both into one grid cell.
Buttons use a consistent 44 px height and 10 px gaps. The style is shared by
S3 and P4; it does not depend on the P4 USB script being present.

## Verification

- 366 MCU/source integration tests.
- 21 JBC source/codec scripts plus 19 plain test functions. Historical exact
  firmware-patch assertions were removed; MCU paths point at MCU_S3 again.
- `Module/JbcUsbModule/tests/run_read_regressions.py` compiles actual firmware
  excerpts and exercises the CL decoder, HA tool/no-tool/busy scheduling and
  service length/model/OTA/UUID/write/rate/queue-expiry guards.
- `tools/tests/test_profile_editor_actions_browser.cjs` checks real action
  markup, current CSS and USB enhancement at 320, 390, 768 and 1440 px in DE/EN,
  with and without USB. It also checks service selection, POST, cooldown and
  rerender behavior. Screenshots are under tools/tests/build-checks/profile-editor-actions.

These are software checks. Station-specific hardware replies, USB behavior
and actual device updates still require owner testing. No device was flashed.
The supplied proprietary Windows reference is not included in this project.

## Build verification

All three builds completed and exported their application binaries:

| Firmware | Image bytes | Image chip ID | Export |
| --- | ---: | ---: | --- |
| MCU-S3 2.1.23RC-Developer | 2904880 | 9 | OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/build/esp32.esp32.esp32s3/OpenFumeExtractorMCU_S3.ino.bin |
| MCU-P4 2.1.23RC-Developer | 3563248 | 18 | OpenFumeExtractorMaster/OpenFumeExtractorMCU_P4/build/esp32.esp32.esp32p4/OpenFumeExtractorMCU_P4.ino.bin |
| JBC USB 1.1.85RC | 465984 | 9 | Module/JbcUsbModule/build/esp32.esp32.esp32s3/JbcUsbModule.ino.bin |

Image identity/version markers were checked. The new profile action CSS and
manual service UI were also found in both MCU application images. Existing
USB picker, USB manager and S3 shared-form browser suites passed as well.
The other modules/displays only received the additive shared action enum;
they do not need an update to use these changes.
