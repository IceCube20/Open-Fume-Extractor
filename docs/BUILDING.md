# Building from Source

The repository contains Arduino sketches for the master, modules and both
displays. Generated build directories and the private release-signing key are
deliberately excluded.

## Tested toolchain

- Arduino IDE 2.x
- Arduino-ESP32 3.3.11 for the master, standard modules and 320x480 display
- Arduino-ESP32 3.2.0 plus the pinned high-performance SDK for the 800x480 display
- Adafruit NeoPixel 1.15.5
- LVGL 9.5.0
- GFX Library for Arduino 1.6.7
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

