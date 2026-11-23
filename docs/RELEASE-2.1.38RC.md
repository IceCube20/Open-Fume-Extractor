# Open Fume Extractor 2.1.38 RC

Published as a regular GitHub release, not a prerelease. The firmware's existing
RC/beta version labels remain unchanged. Private developer configuration and
Developer binaries are not included.

## MCU and Modules

- Community MCU-S3, MCU-P4 PRE-V3 and MCU-P4 WIFI6-PoE-ETH V3.1+ images share
  firmware 2.1.38RC. The P4 variants retain separate hardware and silicon profiles.
- Add the Waveshare PoE board to the firmware catalog with 32 MB flash support.
- Improve powered-module rediscovery after changing MCUs and resynchronize
  power-saving state after reconnecting.
- Allow complete additional routing rules without a selected main input and
  use consistent JBC Work/Stand labels in the shared signal catalogs.
- Keep module-card warnings yellow, errors red and update progress blue.
- Per-module connection-warning preferences support JBC, Weller, Universal
  RS232 and Modbus. Optional downstream disconnects can be muted. A disconnected
  required main output remains an error; unrelated fault bits are not muted.
- Give connection monitoring its own module-table column, separate from restart.
- Display Home cards and module lists show target-specific update progress.
- Include current module/display firmware exports and the native HA package.
  The integration remains 0.1.31rc4; no integration update is required solely
  for this firmware release.

## Hosted C6 Recovery

Include the standalone [Waveshare PoE P4 LAN/SDIO recovery sketch](../OpenFumeExtractorMaster/C6_Update_P4_Waveshare_LAN/README.md).
The online C6 catalog includes a distinct PoE-board approval for the tested
Arduino 3.3.12 / IDF 5.5.5 / Hosted/C6 2.12.13 combination and exact image pins.
It requires Arduino-ESP32 3.3.12 and an explicit serial UPDATE command. It runs
on P4 and temporarily replaces the MCU application, which must be flashed back
afterwards. It is not a generic UART recovery tool and cannot recover every C6
firmware state. Prefer the integrated MCU updater when available.

## Installation

Use the exact board's signed application `.bin` for OTA. Use a matching
`*-merged.bin` only for initial USB flashing at `0x0`. Do not upload a merged
image through OTA or interchange S3, PRE-V3 P4, and V3.1+ PoE P4 images.

See [firmware files and checksums](FIRMWARE.md).
