# Firmware Files

Install a complete merged image with the [GitHub Pages web flasher](https://icecube20.github.io/Open-Fume-Extractor/).

Release tree generated: **2026-11-15**

The normal `.bin` files are Ed25519-signed OTA packages for the master web updater.
The `*-merged.bin` files are complete images for an initial USB flash at address `0x0`.

| Target | Version | Signature target | OTA file | Merged file |
|---|---:|---|---|---|
| OpenFumeExtractor-Master | `2.1.9RC` | `MASTER` | [download](../firmware/OpenFumeExtractor-Master/OpenFumeExtractor-Master-2.1.9RC.bin) | [download](../firmware/OpenFumeExtractor-Master/OpenFumeExtractor-Master-2.1.9RC-merged.bin) |
| OpenFumeExtractor-MCU-P4 | `2.1.9RC` | `MASTER` | [download](../firmware/OpenFumeExtractor-MCU-P4/OpenFumeExtractor-MCU-P4-2.1.9RC.bin) | [download](../firmware/OpenFumeExtractor-MCU-P4/OpenFumeExtractor-MCU-P4-2.1.9RC-merged.bin) |
| JBC-FAE-Bus | `1.1.70RC` | `JBC_BUS` | [download](../firmware/JBC-FAE-Bus/JBC-FAE-Bus-1.1.70RC.bin) | [download](../firmware/JBC-FAE-Bus/JBC-FAE-Bus-1.1.70RC-merged.bin) |
| JBC-USB | `1.1.84RC` | `JBC_USB` | [download](../firmware/JBC-USB/JBC-USB-1.1.84RC.bin) | [download](../firmware/JBC-USB/JBC-USB-1.1.84RC-merged.bin) |
| Fan-IO | `1.1.76RC` | `FAN_IO` | [download](../firmware/Fan-IO/Fan-IO-1.1.76RC.bin) | [download](../firmware/Fan-IO/Fan-IO-1.1.76RC-merged.bin) |
| Fan-IO-Pro | `1.1.71RC` | `FAN_IO_PRO` | [download](../firmware/Fan-IO-Pro/Fan-IO-Pro-1.1.71RC.bin) | [download](../firmware/Fan-IO-Pro/Fan-IO-Pro-1.1.71RC-merged.bin) |
| Weller-Zero-Smog | `1.1.84RC` | `WELLER_ZERO_SMOG` | [download](../firmware/Weller-Zero-Smog/Weller-Zero-Smog-1.1.84RC.bin) | [download](../firmware/Weller-Zero-Smog/Weller-Zero-Smog-1.1.84RC-merged.bin) |
| Display-320x480 | `1.4.37RC` | `DISPLAY_320X480` | [download](../firmware/Display-320x480/Display-320x480-1.4.37RC.bin) | [download](../firmware/Display-320x480/Display-320x480-1.4.37RC-merged.bin) |
| Display-800x480 | `1.4.53RC` | `DISPLAY_800X480` | [download](../firmware/Display-800x480/Display-800x480-1.4.53RC.bin) | [download](../firmware/Display-800x480/Display-800x480-1.4.53RC-merged.bin) |
| Display-P4-800x480 | `1.0.16RC` | `DISPLAY_P4_800X480` | [download](../firmware/Display-P4-800x480/Display-P4-800x480-1.0.16RC.bin) | [download](../firmware/Display-P4-800x480/Display-P4-800x480-1.0.16RC-merged.bin) |
| Display-ST7796-320x480 | `1.0.47RC` | `DISPLAY_ST7796_320X480` | [download](../firmware/Display-ST7796-320x480/Display-ST7796-320x480-1.0.47RC.bin) | [download](../firmware/Display-ST7796-320x480/Display-ST7796-320x480-1.0.47RC-merged.bin) |
| Universal-RS232 | `1.0.74beta` | `UNIVERSAL_RS232` | [download](../firmware/Universal-RS232/Universal-RS232-1.0.74beta.bin) | [download](../firmware/Universal-RS232/Universal-RS232-1.0.74beta-merged.bin) |
| Modbus-RTU | `1.0.61beta` | `MODBUS_RTU` | [download](../firmware/Modbus-RTU/Modbus-RTU-1.0.61beta.bin) | [download](../firmware/Modbus-RTU/Modbus-RTU-1.0.61beta-merged.bin) |

## SHA-256 checksums

- `d21d144e6b90ea02a89688ddc23bd1750a4bfd0a38ef6f0b9ceba3c59ec8ff6d`  `OpenFumeExtractor-Master-2.1.9RC.bin`
- `58e62010d4980a927a6a6f0b4a71f72decd000a7d2ae62738dd4b7a712c44187`  `OpenFumeExtractor-Master-2.1.9RC-merged.bin`
- `56d2b268e8f743c0a6f207f142983c6a2c87a16fdea184573f65eea3c94bea49`  `OpenFumeExtractor-MCU-P4-2.1.9RC.bin`
- `3c4802a7304fb12498bda75f212fb0926f9f903ec3ad1d591865004b62ffa6a6`  `OpenFumeExtractor-MCU-P4-2.1.9RC-merged.bin`
- `bffb0a7a865b351ea871b5baf15d50f713e752599bdd3872ff52319d26dd3c62`  `JBC-FAE-Bus-1.1.70RC.bin`
- `2ba514978c9cda517f77338c16bc642b130f12c009b7a1534baca2eb42839ae2`  `JBC-FAE-Bus-1.1.70RC-merged.bin`
- `ca554b5fd20e80337c35b771da9c13cc8081f12892c71e808d714bf53a3cf53a`  `JBC-USB-1.1.84RC.bin`
- `baf52b74915bd06d572ca3ea41b4cbca4e7a16e1d755eb2e21db643473e1a115`  `JBC-USB-1.1.84RC-merged.bin`
- `d9d0ef1a44e06ab34e8d34292aa8b29cc9dde6dff64a90628d8559e2109a31f7`  `Fan-IO-1.1.76RC.bin`
- `28dfc97cd49dd4a974e4c1ee46bcd7f5c58bfdea31f4fef4fefd4e04c284f989`  `Fan-IO-1.1.76RC-merged.bin`
- `65f09b41ebb3e88215f4190f38bab8f31557a1feec83799b67f6df71b383b9ff`  `Fan-IO-Pro-1.1.71RC.bin`
- `1717b628b3c039e227c442dff90cc4feb8aa852a4c3c05eacfbf5a269b7453e7`  `Fan-IO-Pro-1.1.71RC-merged.bin`
- `5bb8d815ad4e5b6781a20084e8e085d99917d94d0491e5fb21ce68fa76637a52`  `Weller-Zero-Smog-1.1.84RC.bin`
- `3befa2c023f212fd9593ae2c02fbe139a180eddb159a1a16634188458210b494`  `Weller-Zero-Smog-1.1.84RC-merged.bin`
- `947d6c2c2e692d27c39c8fe29ff8f3896b8d8ad2e9ea81167c4b61ca0ac375c3`  `Display-320x480-1.4.37RC.bin`
- `8566f4195b58448a7aededc36838be01f7e3647471564754273f2e75ff215e94`  `Display-320x480-1.4.37RC-merged.bin`
- `eff2701200c698fb1ae55835764d1c4f2a54fa3f0e2484420c0edf3b5bea394c`  `Display-800x480-1.4.53RC.bin`
- `31a05ea464d1fc3e76de835452600002cda75f3c70fc6e47c4429116b4b6cf06`  `Display-800x480-1.4.53RC-merged.bin`
- `2f69a3aa5bb2f2abfb8f6e4062559ff10e73cfb334c218f3b9b82e2daf1f5782`  `Display-P4-800x480-1.0.16RC.bin`
- `cb23c02a203a65468c5f044a35d8f5baf297d49999ccd765fd679fdee2cc15b6`  `Display-P4-800x480-1.0.16RC-merged.bin`
- `aa443a09ffe17dca9af97e41d675774e84db48d255456e4c84a4007bfd342aa0`  `Display-ST7796-320x480-1.0.47RC.bin`
- `7cffbf3a9078fd3244ea80f4101390780962bc0ccaf344c638acdc115f7f308d`  `Display-ST7796-320x480-1.0.47RC-merged.bin`
- `78962484615fa8a8f16ac90e9c12bcfd0f115cf2611534f9eebd3fa0ec3f1cb2`  `Universal-RS232-1.0.74beta.bin`
- `e729ace77fc06312679a027f78108c86789431d4567a87a21bbe77d9facbeee0`  `Universal-RS232-1.0.74beta-merged.bin`
- `20d34d3dfce9aeb75d95f3c9e4d349c58665280f306599a0945cddefb6732a50`  `Modbus-RTU-1.0.61beta.bin`
- `50c128b0b60233088b23e2445bd9c6989bd4b47e7e08794d6425b2a43ee3adba`  `Modbus-RTU-1.0.61beta-merged.bin`

## Update rules

- Never use a merged image in the web updater.
- Never flash an OTA package at address `0x0`.
- Display 320x480, Display 800x480 and Display ST7796 320x480 are different firmware targets.
- Display P4 800x480 requires the ELECROW PRE-V3 hardware/SDK profile; never use an S3 image on it.
- The web updater rejects a file whose target or Ed25519 signature does not match the selected device.
