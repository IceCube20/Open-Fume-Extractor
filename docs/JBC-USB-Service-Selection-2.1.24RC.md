# Persistent JBC service selection

MCU-S3 and MCU-P4: 2.1.24RC-Developer. JBC USB stays at 1.1.85RC.

The previous periodic update replaced the complete SOLD station HTML using
innerHTML, destroying the native service select even when its chosen value
was retained in the selection map. This closed an open dropdown on refresh.

The station card now separates live station values from service controls.
Routine telemetry replaces only the live-value container. The select, button
and status message retain their DOM identity. Disabled states and messages
update in place, including during cooldown and station-link loss.

Changing language, station model/type/version or command protocol rebuilds the
options intentionally. Switching station family removes obsolete controls.
This is a browser rendering fix, not a JBC protocol or task-priority change.

Verification:

- test_jbc_usb_service_refresh_browser.cjs executes the actual station renderer,
  refresh helper and service UI. It opens the native picker and checks focus,
  node identity and live values through 24 successive refreshes, then selection,
  requests, cooldown, messages, disconnect/reconnect and station changes.
- DE/EN at 320, 768 and 1440 px; screenshots under
  tools/tests/build-checks/jbc-usb-service-refresh.
- The actual outer moduleSig is checked: live values do not force a complete
  module card rebuild.
- Source integration guard ensures updateDetails calls the stable refresh path.
- All 367 MCU integration tests and the JBC source/codec suite passed. The
  existing profile-editor/USB browser checks also passed.
- Both MCU firmware builds completed and exported their application binaries.
  Chip IDs, 2.1.24RC-Developer identity and stable service UI markers were
  checked in the exported images. Nothing was flashed.

## Remaining JBC scope

The ordinary status/read paths are broadly implemented, not a complete clone
of every Windows feature. SOLD/HA file/profile transfer and file enumeration
remain absent. OFE caches the connected tool rather than all configured tool
types and limits SOLD peripherals to four entries. StationDateTime remains
blocked after the earlier DDE observation. Read-and-clear alarm and reset
actions are deliberately not offered as harmless automatic reads. Special
internal DME reset-test paths and JBC-station bootloader programming are not
the same as normal monitoring or OFE-module OTA.
