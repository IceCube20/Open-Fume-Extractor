# Firmware Files

Install a complete merged image with the [GitHub Pages web flasher](https://icecube20.github.io/Open-Fume-Extractor/).

Release tree generated: **2026-11-24**

The normal `.bin` files are Ed25519-signed OTA packages for the master web updater.
The `*-merged.bin` files are complete images for an initial USB flash at address `0x0`.

| Target | Version | Signature target | OTA file | Merged file |
|---|---:|---|---|---|
| OpenFumeExtractor-Master | `2.1.52RC` | `MASTER` | [download](../firmware/OpenFumeExtractor-Master/OpenFumeExtractor-Master-2.1.52RC.bin) | [download](../firmware/OpenFumeExtractor-Master/OpenFumeExtractor-Master-2.1.52RC-merged.bin) |
| OpenFumeExtractor-MCU-P4 | `2.1.52RC` | `MASTER` | [download](../firmware/OpenFumeExtractor-MCU-P4/OpenFumeExtractor-MCU-P4-2.1.52RC.bin) | [download](../firmware/OpenFumeExtractor-MCU-P4/OpenFumeExtractor-MCU-P4-2.1.52RC-merged.bin) |
| OpenFumeExtractor-MCU-P4-POE | `2.1.52RC` | `MASTER` | [download](../firmware/OpenFumeExtractor-MCU-P4-POE/OpenFumeExtractor-MCU-P4-POE-2.1.52RC.bin) | [download](../firmware/OpenFumeExtractor-MCU-P4-POE/OpenFumeExtractor-MCU-P4-POE-2.1.52RC-merged.bin) |
| JBC-FAE-Bus | `1.1.71RC` | `JBC_BUS` | [download](../firmware/JBC-FAE-Bus/JBC-FAE-Bus-1.1.71RC.bin) | [download](../firmware/JBC-FAE-Bus/JBC-FAE-Bus-1.1.71RC-merged.bin) |
| JBC-USB | `1.1.86RC` | `JBC_USB` | [download](../firmware/JBC-USB/JBC-USB-1.1.86RC.bin) | [download](../firmware/JBC-USB/JBC-USB-1.1.86RC-merged.bin) |
| Fan-IO | `1.1.78RC` | `FAN_IO` | [download](../firmware/Fan-IO/Fan-IO-1.1.78RC.bin) | [download](../firmware/Fan-IO/Fan-IO-1.1.78RC-merged.bin) |
| Fan-IO-Pro | `1.1.73RC` | `FAN_IO_PRO` | [download](../firmware/Fan-IO-Pro/Fan-IO-Pro-1.1.73RC.bin) | [download](../firmware/Fan-IO-Pro/Fan-IO-Pro-1.1.73RC-merged.bin) |
| Weller-Zero-Smog | `1.1.85RC` | `WELLER_ZERO_SMOG` | [download](../firmware/Weller-Zero-Smog/Weller-Zero-Smog-1.1.85RC.bin) | [download](../firmware/Weller-Zero-Smog/Weller-Zero-Smog-1.1.85RC-merged.bin) |
| Display-320x480 | `1.4.42RC` | `DISPLAY_320X480` | [download](../firmware/Display-320x480/Display-320x480-1.4.42RC.bin) | [download](../firmware/Display-320x480/Display-320x480-1.4.42RC-merged.bin) |
| Display-800x480 | `1.4.58RC` | `DISPLAY_800X480` | [download](../firmware/Display-800x480/Display-800x480-1.4.58RC.bin) | [download](../firmware/Display-800x480/Display-800x480-1.4.58RC-merged.bin) |
| Display-P4-800x480 | `1.0.21RC` | `DISPLAY_P4_800X480` | [download](../firmware/Display-P4-800x480/Display-P4-800x480-1.0.21RC.bin) | [download](../firmware/Display-P4-800x480/Display-P4-800x480-1.0.21RC-merged.bin) |
| Display-ST7796-320x480 | `1.0.52RC` | `DISPLAY_ST7796_320X480` | [download](../firmware/Display-ST7796-320x480/Display-ST7796-320x480-1.0.52RC.bin) | [download](../firmware/Display-ST7796-320x480/Display-ST7796-320x480-1.0.52RC-merged.bin) |
| Universal-RS232 | `1.0.78beta` | `UNIVERSAL_RS232` | [download](../firmware/Universal-RS232/Universal-RS232-1.0.78beta.bin) | [download](../firmware/Universal-RS232/Universal-RS232-1.0.78beta-merged.bin) |
| Modbus-RTU | `1.0.65beta` | `MODBUS_RTU` | [download](../firmware/Modbus-RTU/Modbus-RTU-1.0.65beta.bin) | [download](../firmware/Modbus-RTU/Modbus-RTU-1.0.65beta-merged.bin) |

## SHA-256 checksums

- `5560d057495de23761001ca474a37228df0261e61e670253c56d7e27d7102c14`  `OpenFumeExtractor-Master-2.1.52RC.bin`
- `e686e97b9be6f952f3c5dd5e062fa656d76ce2677e48b365922de6d8d5c84438`  `OpenFumeExtractor-Master-2.1.52RC-merged.bin`
- `295291b42ebdb28a4c8f08c7e0c4fccd44ea5d4c3fc6022ebf26b92bd2e55c4c`  `OpenFumeExtractor-MCU-P4-2.1.52RC.bin`
- `95cad1153acfd049a668461d32145d45bf8bf438e42d9b17aaafb6e807a380a2`  `OpenFumeExtractor-MCU-P4-2.1.52RC-merged.bin`
- `dbfc40d4ba26e39b4f6f3fe623fd34ba2f75e33853fba2b3bfaabd1ddb9e2965`  `OpenFumeExtractor-MCU-P4-POE-2.1.52RC.bin`
- `f37a74a121f2948472ac1728784a8033757dc723ce8dbba034452a5f629a1daf`  `OpenFumeExtractor-MCU-P4-POE-2.1.52RC-merged.bin`
- `dfb9111db19a323982496df9d1b04204f4af40c626b2e2b0457ef13c95c95326`  `JBC-FAE-Bus-1.1.71RC.bin`
- `d908aba0da44ebe162f08d6116f45eb1b8d7378287ae6781f5973079287fc350`  `JBC-FAE-Bus-1.1.71RC-merged.bin`
- `5b823982152b6421655234bf1038c520ae1d383907b986ab7fb34de9af4b30ec`  `JBC-USB-1.1.86RC.bin`
- `82d99f33e6e4963e94d137df9e2b89b3abc34a09d44322dafed3191a49dd53bd`  `JBC-USB-1.1.86RC-merged.bin`
- `cc0aa1a4fc1b38efc9a39d91f25c03003cb768ca633b18522fdc89166f2145d8`  `Fan-IO-1.1.78RC.bin`
- `1f55b83cb77501bf0a8f7d8ef36dcce556c20dfa155cea5563452d274f7edb7e`  `Fan-IO-1.1.78RC-merged.bin`
- `6c43d22e107d0abe538923a18896c99d6c97a1f1e16620180eaf888943f7ab4d`  `Fan-IO-Pro-1.1.73RC.bin`
- `dc5834d9cc5660bbe6268c5db523798e8cf519423bd7a68248bbde9399c0c229`  `Fan-IO-Pro-1.1.73RC-merged.bin`
- `eeff42ccd5e64095d17312633008f3d8bbfe186ee8b099d1f75cef895cf5e26d`  `Weller-Zero-Smog-1.1.85RC.bin`
- `e64cb55c015cd79bd5978e5222dbc9caf4975c2a53aba5105a3f0d6c072bf2ab`  `Weller-Zero-Smog-1.1.85RC-merged.bin`
- `27c9e5559be57595878f7ea18e92f40ac84081f265e9cfb04387660ef1ce00a0`  `Display-320x480-1.4.42RC.bin`
- `c916acef37bc68a078dee9d91a87233a9f9cbe83b5d67cb8b2a306b2dde9f303`  `Display-320x480-1.4.42RC-merged.bin`
- `a7a284f1f7c15fea8eb5c7d24d135de5924cf31ef08f43ad04280c324900867e`  `Display-800x480-1.4.58RC.bin`
- `58d1669ef9b9287748e4360974edb1b59441d7029ad586a7d5ad13f4085349df`  `Display-800x480-1.4.58RC-merged.bin`
- `fc34f19a899763d1ce62c9a4242c3a8a928e2a9a8d5b60fa1e160cff7ce72763`  `Display-P4-800x480-1.0.21RC.bin`
- `056d58c2343636b90b9c2de4c33d6d758b137bb789a68cec478df43b748c4c27`  `Display-P4-800x480-1.0.21RC-merged.bin`
- `4d8328f314b195e5c3dcea82e0b1012b7e6ec244ed408f7339df77b3136289a6`  `Display-ST7796-320x480-1.0.52RC.bin`
- `b9b68d8e2c32d79075fab0dfb95e6d9db8f18b5e4282b92c0e20aaf4eefa1129`  `Display-ST7796-320x480-1.0.52RC-merged.bin`
- `08354439639e65ae0340f9e458f31a058530d481f1a09742842100d25e02e7b2`  `Universal-RS232-1.0.78beta.bin`
- `2fae78913987d038b7fb383668bd00524b988bf59ddc83080448a36b52da26ea`  `Universal-RS232-1.0.78beta-merged.bin`
- `a35ffe6e64fcbf9d93e23d3915e14bc52946cd7bd3e758aa8fcf28fd8a92cf98`  `Modbus-RTU-1.0.65beta.bin`
- `8547a55bc2b351ef7b6f10a5d6b33a892473b3e013e826580add4d034d87072c`  `Modbus-RTU-1.0.65beta-merged.bin`

## Update rules

- Never use a merged image in the web updater.
- Never flash an OTA package at address `0x0`.
- Display 320x480, Display 800x480 and Display ST7796 320x480 are different firmware targets.
- Display P4 800x480 requires the ELECROW PRE-V3 hardware/SDK profile; never use an S3 image on it.
- MCU-P4 PRE-V3 and MCU-P4 WIFI6-PoE-ETH V3.1+ use different silicon/board profiles; choose the exact board.
- The web updater rejects a file whose target or Ed25519 signature does not match the selected device.
