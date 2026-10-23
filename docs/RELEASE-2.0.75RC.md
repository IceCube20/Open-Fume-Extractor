# Open Fume Extractor 2.0.75 RC

Community Master **2.0.75RC** and native Home Assistant integration **0.1.0rc25**.
The public master excludes developer mode and private developer credentials.
Release-signing private keys and large SDK archives are not published.

## Highlights Since 2.0.53 RC

- Direct Bluetooth provisioning between master and display, with matching
  numeric confirmation on both devices, continued discovery and a search timer.
  Manual WiFi provisioning and RS485 remain available.
- Per-display automatic backlight shutdown: Off, Only in power saving or Always,
  with a 1-60 minute dropdown. Unsupported firmware hides these controls in the
  master, MQTT and native integration. When the backlight is off, the first
  touch restores light; release and touch again to leave the screensaver.
- Dedicated GUI ownership on all four display ports. Bus and WiFi processing
  remain separate from LVGL. The UI falls back to the Arduino loop if the GUI
  task cannot start.
- Revised module details, JBC station controls, Home cards and module telemetry;
  improved consistency across both display sizes without removing functionality.
- Improved display value streaming, scheduling and RS485/WiFi diagnostics,
  including developer-only timing and per-core CPU information on the master.
- Profile validation before Universal RS232/Modbus profile transfers.
- Initial experimental ELECROW 5-inch ESP32-P4 RGB target, with ESP-Hosted C6
  networking/Bluetooth, the tested RNG/ECC fixes and a separate high-performance
  SDK build profile. This is not interchangeable with either S3 RGB firmware
  or other P4 board revisions.
- Native HA automatically registers/updates its bundled JavaScript resource in
  Lovelace UI/storage mode. YAML resources and dashboard layouts are untouched.

## Firmware And Installation

The release includes individual **signed OTA `.bin`** packages and complete
**`*-merged.bin`** USB images for all twelve targets. Select the exact hardware
in the [firmware index](FIRMWARE.md) or the
[online flasher](https://icecube20.github.io/Open-Fume-Extractor/).

- Use normal signed `.bin` packages only in the master web updater.
- Use merged images only for initial USB flashing at `0x0`, never as OTA files.
- The P4 package remains **1.0.7alpha-P4Prep**, explicitly experimental, and
  targets the ELECROW 5-inch 800x480 PRE-V3 profile. Use a current P4-aware master.
- Unchanged module version numbers are intentional; they were not relabelled
  simply to match the master release. See checksums in the firmware index.
- A fresh USB erase clears saved settings. Back up settings before changing a
  partition layout. OTA normally preserves the existing configuration.

For HA, update through the dedicated HACS custom repository or install the
attached ZIP. Restart Home Assistant and fully reload the browser/app. Normal
UI-managed dashboard resources no longer need manual version edits. In YAML
resource mode, configure the unversioned bundle URL once as documented in the
[HA guide](../HomeAssistant/README.md).

## Tested Display Profiles

The QSPI release uses the working full-frame path, B32 and 60 MHz. Partial
AXS15231B updates are not enabled. The experimental overlapped transfer path
was slower in the owner's test and is disabled by default.

The ST7796 release uses the working direct dirty-rectangle SPI path at 80 MHz.
The experimental DMA path that produced a black panel is disabled by default.

The owner's OFE benchmark measured approximately 20 FPS on S3 RGB, 41 FPS on
P4, 15 FPS on QSPI and 16 FPS on ST7796, with different panel resolutions and
transport costs. These are scene-specific LVGL measurements, not guarantees
of full-screen physical refresh or performance on every board.

This remains a release candidate. Keep a known-working firmware and test bus,
WiFi, touch, pairing, screensaver and updates on your installation.

## Publication Checks

- Fresh Community master compile and link with Arduino-ESP32 3.3.12.
- 242 firmware regression tests: no failures, one environment-dependent skip.
- 180 main-project HA/MQTT contract tests, plus 145 dedicated-integration tests.
- Native GUI ownership/fallback, QSPI transfer, master runtime/CPU/JSON and
  HA API/MQTT availability tests passed.
- Shared bus and status-LED headers match across master and all modules.
- Every OTA package is Ed25519 verified; its raw application matches the
  corresponding merged image at `0x10000`. Target/version markers, manifests,
  SHA-256 checksums and private-data exclusions are checked before upload.

Build and host tests are not a substitute for long-running hardware tests.
