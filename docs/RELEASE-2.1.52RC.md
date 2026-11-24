# Open Fume Extractor 2.1.52 RC

This is a regular GitHub release, not a prerelease. Existing RC/beta firmware
labels are retained. Published MCU images are community builds: no private
configuration, developer passwords or Developer binaries are included.

## MCU Web Interface

- Update MCU-S3, MCU-P4 PRE-V3 and MCU-P4 WIFI6-PoE-ETH V3.1+ to 2.1.52RC.
  Keep separate chip, silicon-revision and flash-layout checks for each image.
- Add a persistent light/dark theme with consistent controls, module icons,
  calibration badges, update progress and logic simulation/live highlights.
- Improve desktop widths, five-column status layouts, system separators,
  mobile headers and compact connection-monitor controls in the module list.
- Improve Boolean Designer wire routing, stable connection paths, snapping,
  multi-block movement and multi-wire selection. Document graph capacity limits
  and show validation errors before saving oversized graphs.
- Preserve expanded routing/system input and output catalogs and their aliases.
- Use the shared module alarm policy for physical and virtual MCU EVT LEDs.
  Muted optional downstream links no longer cause hidden yellow warnings;
  offline modules, real faults and required disconnected outputs remain visible.

## Hosted C6

- Approve C6 2.13.1 with the existing Hosted 2.12.13 driver only for the tested
  Waveshare PoE silicon 3.2 / 32 MiB combination. PRE-V3 and other board
  combinations retain their existing approvals.
- Use the versioned approval catalog without changing older MCU catalog checks.
  The daily upstream watcher remains discovery-only and does not auto-approve
  or flash new images.
- Private developer builds support numeric Espressif version selection on both
  P4 profiles, with bounded image staging, chip/application/version/Git-blob
  checks and separate installation/downgrade consent. This developer feature
  is not enabled in the published community firmware.
- Preserve Ethernet-only installation, authenticated ownership, update/radio
  exclusions and the direct UART recovery warning. A working SDIO transport is
  required for a return update; recovery is never guaranteed automatically.

## Modules and Displays

- Fan/IO 1.1.78RC and Fan/IO Pro 1.1.73RC add configurable WS2812/SK6812-style
  virtual LED outputs through the existing hardware pin editor and OFE outputs.
  See [LED output configuration](FANIO_LED_OUTPUTS.md).
- Universal RS232 and Modbus RTU distinguish local link loss from repeated
  response timeouts. Suppressed optional connection warnings do not hide faults
  on a required main output or unrelated diagnostic faults.
- Display alarm lists use tighter spacing consistent with module lists.
  Include current exports for all four display variants and all bus modules.

## Home Assistant

The native integration remains 0.1.31rc4: no integration code change was needed
for this release. Its current manual-install archive is included. HACS users
can continue using the existing integration and dashboards.

LED effects respect the browser's reduced-motion preference. If colors update
but animations stop on only one client, check that client's accessibility
animation settings; an MCU update is not needed for that setting.

## Documentation

Expand the German commissioning, operation/diagnostics and firmware-build guides,
and the English MCU, installation, operation, module, display, profile and
troubleshooting manuals. New web-interface and USB guides explain daily workflows,
file/folder handling and separate import/update confirmations. LED outputs now
include a first-indicator walkthrough; automation documents capacity, group
selection and manual wire routes. Correct the community first-login/reset
instructions to match the compiled bootstrap credentials.

Start at the [documentation index](README.md).

## Installation

Use the exact target's signed application `.bin` for OTA. Use its matching
`*-merged.bin` only for initial USB flashing at `0x0`. Do not interchange S3,
PRE-V3 P4, PoE V3 P4 or any of the display images.

See [firmware files and checksums](FIRMWARE.md),
[C6 approvals and recovery](Hosted-C6-Catalog.md) and
[system automation](System-Automation.md).
