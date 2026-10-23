# Building from Source

The repository contains Arduino sketches for the master, modules and all four
display variants. Generated build directories and the private release-signing key are
deliberately excluded.

## Tested toolchain

- Arduino IDE 2.x
- Arduino-ESP32 3.3.12 for the master and standard targets (older tested images used 3.3.11)
- Arduino-ESP32 3.2.0 plus the pinned high-performance SDK for the 800x480 display
- Adafruit NeoPixel 1.15.5
- LVGL 9.6.0 for the current display builds
- GFX Library for Arduino 1.6.9 for the current display builds
- PubSubClient3 3.3.0

Install the libraries through Arduino Library Manager where available. The JBC
USB target uses ESP-IDF USB host APIs supplied by Arduino-ESP32.

## Sketches

Open the matching sketch folder directly in Arduino IDE:

- `OpenFumeExtractorMaster/OpenFumeExtractorMaster.ino`
- `Module/JbcBusModule/JbcBusModule.ino`
- `Module/JbcUsbModule/JbcUsbModule.ino`
- `Module/FanIoModule/FanIoModule.ino`
- `Module/FanIoProModule/FanIoProModule.ino`
- `Module/WellerZeroSmogModule/WellerZeroSmogModule.ino`
- `Module/UniversalRs232Module/UniversalRs232Module.ino`
- `Module/ModbusRtuModule/ModbusRtuModule.ino`
- `Module/DisplayModule_320x480/DisplayModule_320x480.ino`
- `Module/DisplayModule_800x480/DisplayModule_800x480.ino`
- `Module/DisplayModule_ST7796_320x480/DisplayModule_ST7796_320x480.ino`
- `Module/DisplayModule_P4_800x480/DisplayModule_P4_800x480.ino` (experimental)

The master uses the included 16 MB partition CSV. A partition-layout change
requires a complete merged USB flash and normally an erase.

## 800x480 high-performance display

The RGB display requires 64-byte ESP32-S3 data-cache lines and PSRAM XIP. The
repository does not duplicate the large SDK archive. Run:

```powershell
.\tools\build_display_800x480_high_perf.ps1
.\tools\install_display_800x480_arduino.ps1
```

The scripts download pinned official Espressif archives, verify SHA-256 hashes
and build in `Documents\Arduino\ofe-high-performance-sdk`. Restart Arduino IDE
and select `OFE RGB High Performance > OFE Display 800x480`.

## P4 High-Performance Profile (Experimental)

The ELECROW 5-inch RGB port is separate from the S3 port and uses the
**Arduino-ESP32 3.3.12 / ESP-IDF 5.5.5 PRE-V3** SDK. A later chip revision
requires a compatible build, not a forced PRE-V3 setting.

The [P4 profile guide](../tools/boards/p4_high_perf_sdk/README.md) documents the
pinned builder, cache configuration and validation steps. Build and validate
the SDK outside the repository first. Then run
`tools/install_display_p4_arduino.ps1` with paths to that validated SDK and
runtime. This installer does not create a missing SDK or replace the stock one.
Restart Arduino IDE and select **OFE P4 High Performance > OFE Display P4
800x480 (High Performance, PRE-V3)**.

The separate board menu offers Software, PPA fills and PPA fills + images.
Unsupported drawing still uses software; PPA does not accelerate every LVGL
operation. The owner's exported alpha uses the fills + images profile and
24 MHz panel clock. Keep the matching partition layout for OTA.

`tools/boards/prepare_elecrow_p4.py --check` verifies the generated P4 sketch
against the RGB source and P4 templates. Do not regenerate over local changes
without reviewing them. SDK archives, build caches and device flash backups
must stay outside the public repository.

## Firmware signing

`tools/ofe_firmware_sign.py` and the graphical signing application support the
OFE signed package format. The official release private key is not published.
Create a separate Ed25519 key for community builds and keep its private half
outside the repository. The public key compiled into a device must match the
key used to sign its update packages.

Unsigned builds can be flashed over USB. Updating them through a release device
requires a package signed by the key trusted by that device or an explicitly
enabled developer update path.

## Tests

Source-level and native tests are stored below `Module/*/tests` and
`tools/tests`. Run the applicable Python, PowerShell or native test harness after
changing protocol parsing, display transport, persistence or firmware signing.

The native Home Assistant integration and its tests are in `HomeAssistant/`.
Its Python tests use `aiohttp`, `voluptuous` and `jinja2`; the native test runner
also verifies the master's HA dispatch and MQTT availability/performance helpers.
