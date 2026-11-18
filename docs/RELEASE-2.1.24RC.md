# Open Fume Extractor 2.1.24 RC

This release updates both public MCU builds to 2.1.24RC and the JBC USB module
to 1.1.85RC. The native Home Assistant integration remains 0.1.31RC
(`0.1.31rc4`); no integration update is required for these firmware changes.

Published as a regular GitHub release, not a prerelease. The existing RC/beta
firmware version markers are retained.

## MCU Interface

- MCU-S3 and MCU-P4 share the updated file fields, Browse hover states and
  responsive profile-editor/export action rows. USB controls remain P4-only.
- MCU-P4 has a dedicated USB file-manager page and contextual open/save dialogs
  instead of repeated full browsers at the bottom of other pages.
- Browse connected drives and folders, upload/download, rename/move, delete
  and eject. Named FAT volumes are shown when available.
- Choose USB destinations for supported configuration, profile, trace and
  diagnostic exports. Firmware files for the MCU, modules and hosted C6 can
  be selected from USB without starting an installation automatically.
- Transfers retain bounded streaming and drive-replacement guards. Firmware
  signature, target and ESP image architecture checks remain in place.
- Ethernet indicators and file/export controls receive consistent spacing.
- JBC service selection stays open and retains focus during live updates.

## JBC USB 1.1.85

- Parse the complete CLM/CLMU connection-mode field instead of treating any
  letter C in a reply as CONTROL mode.
- Keep port-level HA diagnostics reachable when no tool is connected; busy
  transactions retain their scheduler stage for a later retry.
- Developer builds offer a bounded, allowlisted SOLD/P02 service-read selector.
  These queries do not enter CONTROL mode or write station configuration.
  A queued request is not a confirmed station response.
- Keep original JBC peripheral type labels and field decoding. AutoClean is
  reported as supplied by the station, without interpreting it as FAE control.
- Read-and-clear commands, station resets, arbitrary raw commands and JBC
  station firmware programming are not exposed by the service selector.

## Installation

Use the matching signed app `.bin` for OTA and `*-merged.bin` only for initial
USB flashing at `0x0`. Never upload a merged image through OTA. Community
MCU builds exclude local developer credentials; private Developer builds are
not published. Existing display versions are retained.

See [firmware files and checksums](FIRMWARE.md),
[JBC read details](JBC-USB-Reads-And-Editor-Actions-2026-11-17.md) and
[stable service selection](JBC-USB-Service-Selection-2.1.24RC.md).

The proprietary Windows reference used for comparison is not distributed.
