# Waveshare ESP32-P4 Master Preparation

Status: MCU-P4 release candidate (`2.1.7RC`), previously labelled P4-Preview.
Flashing/setup, network switching and OTA have been reported working by the
owner on PRE-V3 hardware; other board revisions remain unverified. The S3
variant remains supported. No SDK installation, automatic flash or GitHub
release is made by the preparation tool.

## Hardware

Target: **Waveshare ESP32-P4-Module-DEV-KIT**, not P4-WIFI6 and not the Elecrow
display board. The manufacturer documents a P4 with 32 MB PSRAM, 16 MB flash,
an SDIO-connected ESP32-C6 for WiFi/Bluetooth, and a 100 Mbps Ethernet PHY.

- [Board documentation](https://docs.waveshare.com/ESP32-P4-Module-DEV-KIT)
- [Carrier schematic](https://files.waveshare.com/wiki/ESP32-P4-Module-DEV-KIT/ESP32-P4-Module-DEV-KIT.pdf)
- [Manufacturer examples](https://github.com/waveshareteam/ESP32-P4-Platform)

The owner's board is believed to be older than revision 3. Confirm the chip
revision using esptool `chip-id` before flashing. PRE-V3 and v3 SDK images are
not interchangeable. A board photograph is still useful for connector checks.

| Connection | P4 GPIO / configuration |
| --- | --- |
| RS485 transceiver RO -> master RX | GPIO21 |
| RS485 transceiver DI <- master TX | GPIO20 |
| RS485 speed | 250000 baud, 8N1, same automatic-direction transceiver as S3 |
| C6 SDIO CLK / CMD / D0 / D1 / D2 / D3 / reset | 18 / 19 / 14 / 15 / 16 / 17 / 54 |
| IP101 PHY | address 1, MDC31, MDIO52, reset51, external RMII clock50 |
| RMII TX_EN / TXD0 / TXD1 / RXD0 / RXD1 / CRS | 49 / 34 / 35 / 29 / 30 / 28 |
| External OFE status LED | enabled by default on GPIO4 in 2.1.0; external LED required |
| USB-A | high-speed USB OTG host; carrier jumper in HOST position |
| UART0 programming/debug bridge | TX37 / RX38, 115200 baud monitor |

Use 3.3 V-compatible transceiver logic and a common ground. Do not connect A/B
directly to GPIOs. There is no new DE/RE control implementation: an explicitly
direction-controlled transceiver needs a separate adaptation.

**Never run the S3 and P4 as masters on the same RS485 bus at the same time.**
First boot the P4 on USB without the bus. Disconnect/power down the S3 before
moving the transceiver. Do not restore a complete S3 flash image onto a P4.

## Shared Code

`tools/boards/prepare_master_p4.py` stages the existing master into
`MasterP4/OpenFumeExtractorMaster_P4/`. Changes belong in the canonical master;
regenerate afterwards. A manifest detects edited generated files. Private
configuration, build binaries and the S3 NimBLE allocator override are excluded.

The shared logic, web UI, native HA API, MQTT, RS485 scheduling, display tunnel,
module OTA and settings remain in one source tree. P4-specific additions are:

- Waveshare pin profile with conflict checks, PRE-V3/SDK guards.
- Ethernet DHCP with WiFi/recovery AP available; Ethernet takes precedence.
- Network-aware MQTT, display tunnel and published configuration URLs.
- Automatic WiFi shutdown on stable Ethernet, recovery on LAN loss and a
  transport-specific header icon; see [2.0.97 network changes](Master-LAN-Priority-2.0.97RC.md).
- P4 factory eFuse identity, not the companion C6 WiFi MAC.
- SDK NimBLE central over ESP-Hosted, bounded discovery, numeric comparison on
  both devices, authenticated encrypted connection and exact display UID check
  before credentials are sent. Uses the existing tested P4 ECC/RNG backport.
- Chip-specific streamed OTA-header validation, including split HTTP headers;
  developer mode does not bypass the chip check.

Since 2.0.98, LAN and WLAN have separate DHCP/static IPv4 sections in
Network/System. Existing WLAN settings are preserved and LAN defaults to DHCP;
see [separate IPv4 settings](Master-LAN-WLAN-IPv4-2.0.98RC.md).
Since 2.0.99 the status cell also has a sampled real TX/RX LAN activity LED;
see [LAN indicators](Master-LAN-Indicators-2.0.99RC.md).
Since 2.0.100 an explicit staged C6 updater is available on the P4 only:
[C6 updater and LAN activity history](Master-C6-Update-2.0.100RC.md).
BLE display provisioning still needs valid saved **WiFi credentials**
even when the master uses LAN. LAN alone cannot supply a display SSID/password.
Moving between different LAN/WiFi IP addresses can require re-provisioning the
displays' stored master address. This is not a seamless IP-migration feature.

This master has its own UID and starts without the S3's saved settings. Configure
WiFi, MQTT, aliases and routing deliberately; existing HA devices do not
automatically change their identity to the new master. No measured performance
or RAM improvement is promised before an equivalent hardware load test.

Private developer credentials are deliberately not inherited. Developer-only
diagnostics and unsigned OTA need an explicitly configured, local P4
`src/MasterPrivateConfig.h` (see the canonical public example), or use normally
signed firmware for OTA. The first USB upload does not require that file.

## Arduino Build

Requires installed **Arduino-ESP32 3.3.12**, bundled ESP-IDF 5.5.5 and the
existing PubSubClient library. No display/LVGL or high-performance display SDK
is required. The C6 companion must run a firmware compatible with ESP-Hosted.

```powershell
powershell -ExecutionPolicy Bypass -File tools/build_master_p4.ps1
```

This generates and builds only. It does **not** flash. Default serial output
uses the carrier's UART programming bridge. For the P4's native USB CDC port:

```powershell
powershell -ExecutionPolicy Bypass -File tools/build_master_p4.ps1 -NativeUsb
```

In Arduino IDE choose ESP32P4 Dev Module, **Before v3.00**, PSRAM Enabled,
16 MB Flash, Custom partition scheme, Hardware CDC and JTAG. CDC on boot is
Disabled for the UART bridge, Enabled for native USB. Keep the supplied
`partitions.csv`: since 2.1.0 both P4 OTA app slots are `0x500000` bytes (5 MiB),
not 16 MB. The S3 table remains unchanged. Changing from the old P4 table needs
a backup and USB partition-table upload, not an app-only OTA update. Read
[USB storage and partition migration](Master-USB-2.1.0RC.md) before uploading.
The build script enforces that actual app-slot limit. Verify the reported P4
chip revision before the first upload and select the corresponding COM port.

The current generated table targets the owner's 16 MB board. A separate
`tools/boards/master_p4_32mb.csv` is a candidate for the ordered 32 MB board;
do not use it until its actual flash size and silicon revision are verified.
The MCU name alone does not guarantee either memory size.

## First Hardware Checks

### Initial Verified Software Snapshot (2.0.96)

`2.0.96RC-P4-Preview`, Arduino-ESP32 3.3.12 PRE-V3, stock SDK:

- Full linked build passed; application BIN 3,251,808 bytes, OTA slot 3,342,336
  bytes. The slot has 90,528 bytes of remaining image capacity.
- 302 Python regression tests passed.
- S3 sketch/scheduler/display-tunnel compiler checks passed with the MQTT
  worker enabled and disabled; existing native runtime tests passed.
- Native tests exercised the actual P4 central pairing class, confirmation and
  UID rejection, encrypted/authenticated key checks, MTU/timeouts, persistence,
  bounded discovery, network failover, factory UID and split OTA headers.
- Existing P4 display BLE and ECC known-answer tests passed. Final ELF/BIN link
  verification proved the software ECC backport is used, with P4 RNG, and old
  SDK ECC objects are not linked.

Verified local binaries are in the sketch's ignored `build/prev3/` directory.
The app `.ino.bin` alone is not a complete first USB flash. Use the Arduino
upload flow or the complete matching bootloader/partition/app set only after
confirming the chip revision. A merged full-flash image overwrites P4 settings.
No hardware has been flashed by this preparation.

App BIN SHA-256:
`c8a6db4971294f52823b973b7040cb2cfbb791cc277913a3a6e8ef86f288e18b`

### Hardware Validation Pending

1. USB boot without RS485: verify board log, firmware version, chip revision,
   stable factory UID, flash/PSRAM and no reset loop.
2. Verify Ethernet DHCP/web UI, WiFi setup and reconnect after an AP restart.
3. Verify BLE search, both code confirmations, stored display pairing, abort
   and repeated pairing without a master reset; check heap recovery.
4. Move the RS485 transceiver only with the old master off. Verify module
   discovery, control, ECO propagation, faults and safe output behavior.
5. Test MQTT/native HA at identical rates, outputs and routing, all display
   transports, then master/module OTA and aborted updates.
6. Compare CPU per core, minimum/largest heap, loop/scheduler maxima, bus errors
   and update rates under the same load. Finish with a multi-hour soak test.

Source/native tests and a linked build cannot validate wiring, RF operation,
Ethernet PHY reset timing, flash reliability or real-world failsafe behavior.
