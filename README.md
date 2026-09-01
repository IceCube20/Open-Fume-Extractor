# Open Fume Extractor

Open Fume Extractor is a modular fume-extraction controller built around an
ESP32-S3 master, a 250 kbaud OFE RS485 bus, interchangeable interface modules,
two display sizes, a web interface and Home Assistant integration through MQTT.

This repository contains the complete project source, ready-to-flash firmware
images, a browser-based USB flasher and English end-user documentation.

## Start here

1. Open the [browser-based USB flasher](https://icecube20.github.io/Open-Fume-Extractor/) in desktop Chrome or Edge.
2. Read [Installation](docs/INSTALLATION.md).
3. Check the [hardware pinout](docs/PINOUT.md) before wiring anything.
4. Select the exact target in the [firmware index](docs/FIRMWARE.md).
5. Use `*-merged.bin` for an initial USB flash at address `0x0`.
6. Use the matching normal `.bin` for signed web or bus updates.

## Documentation

- [Installation and first commissioning](docs/INSTALLATION.md)
- [Operation and diagnostics](docs/OPERATION.md)
- [Hardware and pinout](docs/PINOUT.md)
- [Firmware files, versions and checksums](docs/FIRMWARE.md)
- [Building from source](docs/BUILDING.md)
- [Universal RS232 and Modbus RTU Profile Builder](docs/PROFILE-BUILDER.md)
- [Display RS485/WiFi operation](docs/DISPLAYS.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)
- [GitHub Pages web flasher setup](docs/GITHUB-PAGES.md)

## Source layout

- `OpenFumeExtractorMaster/` - master firmware
- `Module/` - all RS485 module and display firmware sketches
- `profiles/` - example Universal RS232 and Modbus RTU profiles
- `examples/` - hardware and protocol examples
- `tools/` - build, verification and firmware-signing utilities
- `firmware/` - signed OTA packages and complete merged USB images
- `flasher/` - firmware catalog and ESP Web Tools manifests

## Main features

- persistent module inventory with automatic address assignment
- JBC-compatible FAE bus and USB station monitoring/control
- Fan/IO, Fan/IO Pro and Weller Zero Smog outputs
- profile-driven Universal RS232 and Modbus RTU bridges with up to 32 entities
- main input/output routing, after-run power and graphical Boolean logic
- bilingual web UI and displays
- Home Assistant MQTT Discovery with TLS support
- Ed25519-signed master and module firmware updates
- 320x480 and 800x480 displays with RS485/WiFi failover and wireless OTA

## License

Original Open Fume Extractor source code is licensed under
[GPL-3.0-or-later](LICENSE). Third-party components retain their own licenses;
see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and `LICENSES/`.

The private firmware-signing key is intentionally not published. It is not
required to inspect, modify or build the source. Community builds may use their
own signing key or the developer update path on hardware they control.

## Compatibility and trademarks

This is an independent community project. JBC and other referenced product
names belong to their respective owners and are used only to describe
compatibility. The project is not affiliated with or endorsed by JBC Soldering
S.L. No proprietary JBC firmware, DLL, source code, logo or documentation is
distributed. See [TRADEMARKS.md](TRADEMARKS.md).

## Safety

The firmware and pinout describe 3.3 V logic and low-voltage controller wiring.
They are not a certified mains or motor-power design. Use suitable fusing,
isolation, level shifting, flyback/overvoltage protection and an enclosure for
the actual load. Never connect RS232 voltage levels directly to an ESP32 GPIO.
