# MCU-P4: Waveshare WIFI6-PoE-ETH V3.2

Release profile: MCU 2.1.38RC, or 2.1.38RC-Developer with private developer
configuration. The owner has reported operation on V3.2 hardware, including
C6 recovery with the standalone helper. Automated build/protocol tests do not
replace validation of every peripheral on a particular carrier.

## Hardware

- Board: Waveshare ESP32-P4-WIFI6-POE-ETH; user-reported chip revision V3.2.
- 32 MiB flash and 32 MiB PSRAM. The installed Arduino-ESP32 3.3.12 production
  SDK supports full chip revisions 301 through 399, including V3.2 (302).
- Ethernet: IP101, PHY address 1, MDC 31, MDIO 52, reset 51, external RMII clock.
- C6 SDIO: CLK 18, CMD 19, D0/D1/D2/D3 14/15/16/17, reset 54.
- OFE RS485 defaults: RX GPIO21, TX GPIO20; common ground and 3.3 V UART levels.
- External OFE SK6812 status-LED data: GPIO4, not the board's power indicator.
- USB-A is the native high-speed host connection. USB-C uses the onboard
  UART bridge for flashing/monitoring; leave USB CDC on boot disabled.

GPIO numbers are not physical header pin numbers. The carrier's codec, SD
socket and C6 IO2 wiring are additionally reserved in the PoE pin guard.

Sources: [Waveshare board documentation](https://docs.waveshare.com/ESP32-P4-WIFI6-POE-ETH),
[schematic](https://files.waveshare.com/wiki/ESP32-P4-WIFI6-POE-ETH/ESP32-P4-WIFI6-POE-ETH-Schematic.pdf),
[revision guidance](https://docs.waveshare.com/ESP32-P4-WIFI6-POE-ETH/ESP-IDF).
Reviewed schematic SHA-256: `885b8f038881c6656486fea143bcf749f17c6a1ec9006cc693dcdb298018380b`.

## Build

```powershell
python tools/boards/prepare_master_p4.py --poe
.\tools\build_master_p4.ps1 -Poe
```

Sketch: `OpenFumeExtractorMaster/OpenFumeExtractorMCU_P4_POE/OpenFumeExtractorMCU_P4_POE.ino`.
Arduino IDE: ESP32P4 Dev Module, Chip Variant **v3.00 or newer**, PSRAM enabled,
Flash Size **32 MB**, Custom partition scheme, USB CDC on boot disabled.
The old Module DEV KIT remains a separate **Before v3.00 / 16 MB** profile.
Do not switch a single exported build between these profiles or use `--force`.

For release/OTA exports use the PowerShell build script above. It supplies the
SDK-specific `--min-rev-full` / `--max-rev-full` flags to Espressif's elf2image
tool for both app and bootloader: 301..399 for PoE, 1..199 for PRE-V3. Stock
Arduino IDE exports otherwise contain 0..65535 even when the SDK is selected
correctly. Such ambiguous P4 images are rejected by the new MCU OTA check;
the developer override cannot bypass this. No global IDE installation is changed.
Older P4 app files with unbounded headers must be rebuilt with the script or
installed through the appropriate USB procedure, not downgraded through OTA.

The generator does not copy private credentials or board-local settings. An
existing destination-local private configuration is preserved. Public builds
retain RC; private developer builds retain RC-Developer.

## First Installation And Limits

Use USB for the first install and the complete matching merged image at `0x0`,
or upload the sketch through Arduino IDE. The 32 MiB layout keeps the P4 NVS
and 5 MiB OTA slot offsets, extends LittleFS and moves coredump to the flash end.
Back up settings before migrating a board with existing data. Never use the
merged image in OTA. The MCU image-header check now verifies actual chip
revision ranges as well as S3/P4 chip IDs before allowing an update, including
developer override paths. It does not identify arbitrary carrier-board wiring.

The C6 compatibility catalog includes a separate approval for this board ID,
Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5 / ESP-Hosted 2.12.13 and the exact pinned
C6 image size and SHA-256. The owner reported a successful C6 recovery and
2.12.13 operation. This does not validate every older C6 recovery state or all
network, pairing and USB configurations. Online metadata never starts flashing.
The software ECC/RNG backport retains its pinned Arduino 3.3.12 / IDF 5.5.5 ABI.

No device is flashed by these scripts. Preparing a profile does not publish it.
The 2.1.38RC firmware catalog provides a separate PoE-board installation target.
See the [standalone C6 recovery instructions](../OpenFumeExtractorMaster/C6_Update_P4_Waveshare_LAN/README.md)
when a sufficiently old hosted firmware cannot be updated through the MCU UI.

## Verified Local Build

- `2.1.25RC-Developer`, image chip ID 18, 3,566,800-byte application.
- App and bootloader headers both specify 301..399 and 32 MiB flash.
- Complete 32 MiB merged image contains the selected app, bootloader and
  partition table; build and sketch-export copies match.
- SHA-256 (raw app): `c22904af452a77f21b0cba1ddc153c1b460be734696a19665f90c1da487a5682`.
- 373 Python regressions pass. Native checks cover S3, PRE-V3 and PoE network
  helpers, factory IDs, C6 board identity, SDK compile guards and fragmented
  OTA headers, including rejection of ambiguous P4 images.
- The actual ELF/map uses the pinned software BLE ECC objects and P4 RNG;
  the SDK's replaced ECC objects and accelerator calls are absent.
- Both generated MCU source profiles pass their manifest checks. The public
  GitHub checkout is unchanged. No board was flashed or hardware-tested.

Repeat artifact verification after compiling:

```powershell
python tools/tests/check_master_p4_image.py tools/tests/build-checks/master-p4-poe-v3 --poe
python tools/tests/check_p4_ecc_link.py tools/tests/build-checks/master-p4-poe-v3 --master --poe
```
