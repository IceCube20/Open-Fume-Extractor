# Firmware Files

Install a complete merged image with the [GitHub Pages web flasher](https://icecube20.github.io/Open-Fume-Extractor/).

Release tree generated: **2026-11-18**

The normal `.bin` files are Ed25519-signed OTA packages for the master web updater.
The `*-merged.bin` files are complete images for an initial USB flash at address `0x0`.

| Target | Version | Signature target | OTA file | Merged file |
|---|---:|---|---|---|
| OpenFumeExtractor-Master | `2.1.24RC` | `MASTER` | [download](../firmware/OpenFumeExtractor-Master/OpenFumeExtractor-Master-2.1.24RC.bin) | [download](../firmware/OpenFumeExtractor-Master/OpenFumeExtractor-Master-2.1.24RC-merged.bin) |
| OpenFumeExtractor-MCU-P4 | `2.1.24RC` | `MASTER` | [download](../firmware/OpenFumeExtractor-MCU-P4/OpenFumeExtractor-MCU-P4-2.1.24RC.bin) | [download](../firmware/OpenFumeExtractor-MCU-P4/OpenFumeExtractor-MCU-P4-2.1.24RC-merged.bin) |
| JBC-FAE-Bus | `1.1.70RC` | `JBC_BUS` | [download](../firmware/JBC-FAE-Bus/JBC-FAE-Bus-1.1.70RC.bin) | [download](../firmware/JBC-FAE-Bus/JBC-FAE-Bus-1.1.70RC-merged.bin) |
| JBC-USB | `1.1.85RC` | `JBC_USB` | [download](../firmware/JBC-USB/JBC-USB-1.1.85RC.bin) | [download](../firmware/JBC-USB/JBC-USB-1.1.85RC-merged.bin) |
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

- `69e7adf5d94570dceffa0915e1ad4d982362253994ec6037fd51550951370cc4`  `OpenFumeExtractor-Master-2.1.24RC.bin`
- `e3cb1284b0a37578187b3889bba8357ba1283102c42c2fb9fa635227807324b6`  `OpenFumeExtractor-Master-2.1.24RC-merged.bin`
- `542f85aeb7ed095699771a62ca2bce7289bebed2a30e608e64defaa2551714f9`  `OpenFumeExtractor-MCU-P4-2.1.24RC.bin`
- `f86798b729fc52c8d55c44bd6264ae6c8eba25366b68bd4f46da08e4669a9aa4`  `OpenFumeExtractor-MCU-P4-2.1.24RC-merged.bin`
- `affe2cb07268d300201d4ead527b432d3c7b808c529fd8740edaa883feef23d7`  `JBC-FAE-Bus-1.1.70RC.bin`
- `ede2f826d81e42812ab183a6fc8ab133b31b152c19531c41ff27c2dd0491e3f7`  `JBC-FAE-Bus-1.1.70RC-merged.bin`
- `ae24d7d37e78e2670dc7ccb253aa763caf71d40d3700fb53f76ffd4e367f8870`  `JBC-USB-1.1.85RC.bin`
- `2427307c671a4ff40f60da57567e6897f15757732e6e20d9abba7e8582d2427c`  `JBC-USB-1.1.85RC-merged.bin`
- `ed236a19e14fef086e562354df858b9c90b98dbc7e298c41b79f6a0a0dc8672d`  `Fan-IO-1.1.76RC.bin`
- `7abd51273965104403c9ad24398dd37c303f3c98089c2bd38a3b95b66848f2d1`  `Fan-IO-1.1.76RC-merged.bin`
- `0fdae944c1b95dfe43f7e3c8dc3538516f2940efbe60511040276f5c2ae2403f`  `Fan-IO-Pro-1.1.71RC.bin`
- `0c9a057a859b185aab0b0737298d4d789690f69b5365ca4cae399237a2d710f5`  `Fan-IO-Pro-1.1.71RC-merged.bin`
- `a9c227e7780aee198b59954af11dcbc0a8fa9b0a4ab8484ea69f7d7824f4a5f0`  `Weller-Zero-Smog-1.1.84RC.bin`
- `6434fb024eea979e5f5090cd1d419919fee69ca6d785ac794b82369a1a2b9bc5`  `Weller-Zero-Smog-1.1.84RC-merged.bin`
- `947d6c2c2e692d27c39c8fe29ff8f3896b8d8ad2e9ea81167c4b61ca0ac375c3`  `Display-320x480-1.4.37RC.bin`
- `8566f4195b58448a7aededc36838be01f7e3647471564754273f2e75ff215e94`  `Display-320x480-1.4.37RC-merged.bin`
- `eff2701200c698fb1ae55835764d1c4f2a54fa3f0e2484420c0edf3b5bea394c`  `Display-800x480-1.4.53RC.bin`
- `31a05ea464d1fc3e76de835452600002cda75f3c70fc6e47c4429116b4b6cf06`  `Display-800x480-1.4.53RC-merged.bin`
- `2f69a3aa5bb2f2abfb8f6e4062559ff10e73cfb334c218f3b9b82e2daf1f5782`  `Display-P4-800x480-1.0.16RC.bin`
- `cb23c02a203a65468c5f044a35d8f5baf297d49999ccd765fd679fdee2cc15b6`  `Display-P4-800x480-1.0.16RC-merged.bin`
- `aa443a09ffe17dca9af97e41d675774e84db48d255456e4c84a4007bfd342aa0`  `Display-ST7796-320x480-1.0.47RC.bin`
- `7cffbf3a9078fd3244ea80f4101390780962bc0ccaf344c638acdc115f7f308d`  `Display-ST7796-320x480-1.0.47RC-merged.bin`
- `f1696b62418d51b56056644798a92903078472c039b5cf244600cf7d26b6e74b`  `Universal-RS232-1.0.74beta.bin`
- `d822fc777a3feecd229d9d958c177305e1f08bd6b47b5248b96bd1a5392bfca2`  `Universal-RS232-1.0.74beta-merged.bin`
- `8cc3e8b0c65c497a4bab1035a270f7e8b493854cc669cbc3528a794089e3bde9`  `Modbus-RTU-1.0.61beta.bin`
- `ffeb2fcfac72ea7e942bc2cf9a0a7fa032a72d1dd50d55783dd76cc982b41672`  `Modbus-RTU-1.0.61beta-merged.bin`

## Update rules

- Never use a merged image in the web updater.
- Never flash an OTA package at address `0x0`.
- Display 320x480, Display 800x480 and Display ST7796 320x480 are different firmware targets.
- Display P4 800x480 requires the ELECROW PRE-V3 hardware/SDK profile; never use an S3 image on it.
- The web updater rejects a file whose target or Ed25519 signature does not match the selected device.
