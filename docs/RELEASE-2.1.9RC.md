# Open Fume Extractor 2.1.9 RC

This release adds the MCU-P4 alongside MCU-S3, updates all display targets,
and includes native Home Assistant integration 0.1.31RC (`0.1.31rc4`).

## MCU

- MCU-S3 and MCU-P4 now share `OpenFumeExtractorMaster/`, each with its own
  Arduino sketch folder. Public builds do not contain local private settings.
- MCU-P4 supports 32 bus modules, Ethernet/WiFi failover, USB storage and the
  explicitly staged ESP-Hosted C6 updater on the supported Waveshare PRE-V3 board.
- MQTT default topics now include the full factory serial number. States,
  commands, module data and availability no longer mix between multiple MCUs.
  Broker Client IDs are independent of configured hostnames too.
- Existing MQTT device/entity identifiers are retained. External consumers of
  the old default topic must switch to `open-fume-extractor/<serial>/...`.
  Explicit custom Base Topics are preserved and must be unique per MCU.
- MCU OTA verifies the actual ESP image chip ID. S3 and P4 firmware cannot be
  interchanged, including when signature overrides are enabled.
- Fix MCU OTA completion/abort URLs. Display updates are not affected by this fix.
- Module aliases, fresh-setup input selection and status LED service are updated.

## Displays And Home Assistant

- Updated display inventory capacity, MCU labels and loss-of-connection alarms.
- Native HA includes MCU network diagnostics and correct Ethernet card badges.
- The automatic dashboard has one view per MCU and up to four responsive columns.
  Column changes and resizing preserve live controls; phones remain single-column.
- Native subsecond polling retains its configured cadence without overlapping reads.

## Installation

Use the matching signed app `.bin` for OTA. Use `*-merged.bin` only for an initial
USB flash at `0x0`; never upload it via OTA. Back up settings before changing a
partition layout. See [firmware files and checksums](FIRMWARE.md),
[building from source](BUILDING.md) and the individual MCU notes for board limits.

The C6 compatibility catalog is a separate, pinned update channel; MCU firmware
updates do not silently flash the coprocessor. RS485 diagnostics remain passive,
not an automatic baud-rate sweep or a destructive electrical test.
