# MCU 2.1.6RC: capacity and release preparation

Status: source preparation only. The owner builds the firmware; no Arduino
firmware build, flash, Git commit, push or release is performed in this step.

## Folder Layout

- `OpenFumeExtractorMaster/OpenFumeExtractorMCU_S3/OpenFumeExtractorMCU_S3.ino`
- `OpenFumeExtractorMaster/OpenFumeExtractorMCU_P4/OpenFumeExtractorMCU_P4.ino`

S3 is the shared application source. `tools/boards/prepare_master_p4.py` updates
the P4 adapter without copying private configuration or overwriting local edits.
Build helpers, tests and catalog workflow paths follow the new structure.

## Capacity

- MCU-S3 remains at 16 bus modules; MCU-P4 supports 32.
- Registry, address assignment, saved module snapshots and web/API inventory
  follow the architecture-specific registry capacity. Records remain in PSRAM.
- All four display inventories, list rows, Home cards and fan-animation slots
  cover 32 modules plus the MCU. Output selectors cover the complete inventory;
  input-selector storage and Universal/Modbus caches are expanded as well.
- Existing per-family 16-address ranges, 16 possible display addresses, route
  slots, UART speed, pinouts and wire formats are unchanged. This is a software
  capacity, not a validation of a particular 32-module electrical installation.
- MQTT migrates both 16/32-slot manifests and the earlier 72-entity stride.
  Previously retained discovery entries remain available for stale-entity cleanup.

## Architecture Guard

Both MCUs intentionally retain the signed OTA target `MASTER`. An independent
ESP image-header check compares the full 16-bit chip ID: S3 `0x0009`, P4 `0x0012`.
Wrong-chip, malformed and incomplete headers cannot commit the inactive OTA slot.
The developer signature override does not bypass the chip check. Both the chunk
updater and the older multipart endpoint now reset/check the header and verify
the complete payload. Normal multipart requests also require signed metadata;
use the regular web updater, which supplies it automatically.

USB-stick firmware installation uses that same web update path. Release
preflight independently checks the actual application image's chip ID, source
version, Ed25519 signature and merged-image application bytes. The web-flasher
catalog has distinct ESP32-S3/P4 manifests. External tools with forced raw-flash
options are outside the application's protection; always select the exact board.

## Build Handoff

Run `tools/prepare_community_mcu.ps1` to prepare MCU source copies without private
developer credentials under `tools/tests/build-checks/community-mcu/`.
Open the two `.ino` files there for **public** release builds. The ordinary local
MCU folders may still enable developer mode through the owner's private header.
Export compiled binaries from Arduino IDE; the release helper expects the standard
`build/esp32.esp32.esp32s3` and `build/esp32.esp32.esp32p4` directories in these copies.

Required source versions:

| Target | Version |
| --- | --- |
| MCU-S3 | 2.1.6RC |
| MCU-P4 | 2.1.6RC-P4-Preview |
| Display QSPI 320x480 | 1.4.37RC |
| Display ST7796 320x480 | 1.0.47RC |
| Display S3 RGB 800x480 | 1.4.53RC |
| Display P4 800x480 | 1.0.16RC |

Keep the tested board/SDK/PSRAM settings. No partition changes were made.
After all exports are ready, `tools/prepare_github_release.ps1 -ValidateOnly`
checks the complete set without replacing release files. Stale/missing or
developer-enabled binaries stop publication. HA candidate remains 0.1.31rc2.

## Verification

Native tests compile the actual registry and MQTT migration for both targets,
including full-table UID moves and all 16/32-slot legacy-layout combinations.
The actual multipart handler is exercised with all image-header split points,
both chip IDs, enabled/disabled developer override, truncated data, invalid
metadata, missing payload marker, crypto failure and an already active update.
The P4 BLE/native harness also verifies both chip directions independently.
Source regressions and generator parity are checked without an Arduino build.
Hardware operation with 32 attached modules and new release binaries remains
subject to the owner's builds and installation tests.
