# Firmware Files

Install a complete merged image with the [GitHub Pages web flasher](https://icecube20.github.io/Open-Fume-Extractor/).

Release tree generated: **2026-09-05**

The normal `.bin` files are Ed25519-signed OTA packages for the master web updater.
The `*-merged.bin` files are complete images for an initial USB flash at address `0x0`.

| Target | Version | Signature target | OTA file | Merged file |
|---|---:|---|---|---|
| OpenFumeExtractor-Master | `1.9.29beta` | `MASTER` | [download](../firmware/OpenFumeExtractor-Master/OpenFumeExtractor-Master-1.9.29beta.bin) | [download](../firmware/OpenFumeExtractor-Master/OpenFumeExtractor-Master-1.9.29beta-merged.bin) |
| JBC-FAE-Bus | `1.1.60beta` | `JBC_BUS` | [download](../firmware/JBC-FAE-Bus/JBC-FAE-Bus-1.1.60beta.bin) | [download](../firmware/JBC-FAE-Bus/JBC-FAE-Bus-1.1.60beta-merged.bin) |
| JBC-USB | `1.1.76beta` | `JBC_USB` | [download](../firmware/JBC-USB/JBC-USB-1.1.76beta.bin) | [download](../firmware/JBC-USB/JBC-USB-1.1.76beta-merged.bin) |
| Fan-IO | `1.1.54beta` | `FAN_IO` | [download](../firmware/Fan-IO/Fan-IO-1.1.54beta.bin) | [download](../firmware/Fan-IO/Fan-IO-1.1.54beta-merged.bin) |
| Fan-IO-Pro | `1.1.41beta` | `FAN_IO_PRO` | [download](../firmware/Fan-IO-Pro/Fan-IO-Pro-1.1.41beta.bin) | [download](../firmware/Fan-IO-Pro/Fan-IO-Pro-1.1.41beta-merged.bin) |
| Weller-Zero-Smog | `1.1.74beta` | `WELLER_ZERO_SMOG` | [download](../firmware/Weller-Zero-Smog/Weller-Zero-Smog-1.1.74beta.bin) | [download](../firmware/Weller-Zero-Smog/Weller-Zero-Smog-1.1.74beta-merged.bin) |
| Display-320x480 | `1.3.59beta` | `DISPLAY_320X480` | [download](../firmware/Display-320x480/Display-320x480-1.3.59beta.bin) | [download](../firmware/Display-320x480/Display-320x480-1.3.59beta-merged.bin) |
| Display-800x480 | `1.3.67beta` | `DISPLAY_800X480` | [download](../firmware/Display-800x480/Display-800x480-1.3.67beta.bin) | [download](../firmware/Display-800x480/Display-800x480-1.3.67beta-merged.bin) |
| Universal-RS232 | `1.0.66alpha` | `UNIVERSAL_RS232` | [download](../firmware/Universal-RS232/Universal-RS232-1.0.66alpha.bin) | [download](../firmware/Universal-RS232/Universal-RS232-1.0.66alpha-merged.bin) |
| Modbus-RTU | `1.0.52alpha` | `MODBUS_RTU` | [download](../firmware/Modbus-RTU/Modbus-RTU-1.0.52alpha.bin) | [download](../firmware/Modbus-RTU/Modbus-RTU-1.0.52alpha-merged.bin) |

## SHA-256 checksums

- `b6a183a0e00277355daa7fb57fca907c99f4622cc6ecdf51d26dbdc4ad66f5cd`  `OpenFumeExtractor-Master-1.9.29beta.bin`
- `1f10f4c32b036cc3e50f3ec60cdcc4d0c243b493ee0f7a8e174974a80b025353`  `OpenFumeExtractor-Master-1.9.29beta-merged.bin`
- `2c9960d083e14ddb7d5b91c65b5c1271b4a49b7a79367fa9118d3c53ab960df2`  `JBC-FAE-Bus-1.1.60beta.bin`
- `317701bc6b3d47dbe6c974f03d9a1817ab05d9c03bb7b35330c999a3f5e85794`  `JBC-FAE-Bus-1.1.60beta-merged.bin`
- `db8ae15ce4f4006b68ba370fa9605cf107c474d4efc502532e92274ad5b6adeb`  `JBC-USB-1.1.76beta.bin`
- `48467b93b7ed429003adecc5a4ff872d144ab4e2e9b82835cf0aea23df87551d`  `JBC-USB-1.1.76beta-merged.bin`
- `b1ba793f2dd0325038c10f9e6902a3a4ec3bc658227f79a77a06b0d6d2d3cb2f`  `Fan-IO-1.1.54beta.bin`
- `37ca363aeb3e8f13662d8b939ec058f8bb625d661c0e8cf3b729b24fe67eb47e`  `Fan-IO-1.1.54beta-merged.bin`
- `7b47a2aa56dcf630cb74baaf74a4dba0b36744725e86809be3564a92d7bea6da`  `Fan-IO-Pro-1.1.41beta.bin`
- `a547c5713431acf7f253d4ca675fc7e9f8338ec024cea090dbda75c33f84f7ff`  `Fan-IO-Pro-1.1.41beta-merged.bin`
- `3d71cde8a98afb1874d14ac373067f43f8b0d6e89bdb702b1e573470d35ff13e`  `Weller-Zero-Smog-1.1.74beta.bin`
- `4205079ee895b82060d0856eb2bbbe72cd1291b6d386bf54bd0b70af6bb519c9`  `Weller-Zero-Smog-1.1.74beta-merged.bin`
- `a0abab6f8c2ed6d85377923be7d53d845a37426802b310a7eed0e6764e811617`  `Display-320x480-1.3.59beta.bin`
- `43d682387e8f8f8e92e4b37a7eeb05d033065f4fc16593c778c5b477f4939ac3`  `Display-320x480-1.3.59beta-merged.bin`
- `7e8b63fe2edee82b31ec9a35fcb9232c75cba7638d197c5fe80c1a4e58f1ed5d`  `Display-800x480-1.3.67beta.bin`
- `1ad5cf602b3a6e756e95dc4afb6f3f49e58c3d85f8265c68da0191a2567bfb32`  `Display-800x480-1.3.67beta-merged.bin`
- `244fdd3e2f90c558b3b5f7e8d42e22ca64d8c1afcdaed4fd0945ae7c3f87704e`  `Universal-RS232-1.0.66alpha.bin`
- `0c35b9e235c7d6f07da47f1f7eab967d7b1180c728e6433187e2411fb6688c2b`  `Universal-RS232-1.0.66alpha-merged.bin`
- `13fcbde73316e22034ae0d0e7122cd5a70f56fe5a310f0564529c0434be917aa`  `Modbus-RTU-1.0.52alpha.bin`
- `287b89b1767abc542b6c09e9036b68671be92d90aba8c37bd3814c3ba51eb246`  `Modbus-RTU-1.0.52alpha-merged.bin`

## Update rules

- Never use a merged image in the web updater.
- Never flash an OTA package at address `0x0`.
- Display 320x480 and Display 800x480 are different firmware targets.
- The web updater rejects a file whose target or Ed25519 signature does not match the selected device.
