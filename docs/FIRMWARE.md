# Firmware Files

Install a complete merged image with the [GitHub Pages web flasher](https://icecube20.github.io/Open-Fume-Extractor/).

Release tree generated: **2026-09-06**

The normal `.bin` files are Ed25519-signed OTA packages for the master web updater.
The `*-merged.bin` files are complete images for an initial USB flash at address `0x0`.

| Target | Version | Signature target | OTA file | Merged file |
|---|---:|---|---|---|
| OpenFumeExtractor-Master | `1.9.35beta` | `MASTER` | [download](../firmware/OpenFumeExtractor-Master/OpenFumeExtractor-Master-1.9.35beta.bin) | [download](../firmware/OpenFumeExtractor-Master/OpenFumeExtractor-Master-1.9.35beta-merged.bin) |
| JBC-FAE-Bus | `1.1.60beta` | `JBC_BUS` | [download](../firmware/JBC-FAE-Bus/JBC-FAE-Bus-1.1.60beta.bin) | [download](../firmware/JBC-FAE-Bus/JBC-FAE-Bus-1.1.60beta-merged.bin) |
| JBC-USB | `1.1.77beta` | `JBC_USB` | [download](../firmware/JBC-USB/JBC-USB-1.1.77beta.bin) | [download](../firmware/JBC-USB/JBC-USB-1.1.77beta-merged.bin) |
| Fan-IO | `1.1.54beta` | `FAN_IO` | [download](../firmware/Fan-IO/Fan-IO-1.1.54beta.bin) | [download](../firmware/Fan-IO/Fan-IO-1.1.54beta-merged.bin) |
| Fan-IO-Pro | `1.1.41beta` | `FAN_IO_PRO` | [download](../firmware/Fan-IO-Pro/Fan-IO-Pro-1.1.41beta.bin) | [download](../firmware/Fan-IO-Pro/Fan-IO-Pro-1.1.41beta-merged.bin) |
| Weller-Zero-Smog | `1.1.74beta` | `WELLER_ZERO_SMOG` | [download](../firmware/Weller-Zero-Smog/Weller-Zero-Smog-1.1.74beta.bin) | [download](../firmware/Weller-Zero-Smog/Weller-Zero-Smog-1.1.74beta-merged.bin) |
| Display-320x480 | `1.3.71beta` | `DISPLAY_320X480` | [download](../firmware/Display-320x480/Display-320x480-1.3.71beta.bin) | [download](../firmware/Display-320x480/Display-320x480-1.3.71beta-merged.bin) |
| Display-800x480 | `1.3.77beta` | `DISPLAY_800X480` | [download](../firmware/Display-800x480/Display-800x480-1.3.77beta.bin) | [download](../firmware/Display-800x480/Display-800x480-1.3.77beta-merged.bin) |
| Universal-RS232 | `1.0.66alpha` | `UNIVERSAL_RS232` | [download](../firmware/Universal-RS232/Universal-RS232-1.0.66alpha.bin) | [download](../firmware/Universal-RS232/Universal-RS232-1.0.66alpha-merged.bin) |
| Modbus-RTU | `1.0.52alpha` | `MODBUS_RTU` | [download](../firmware/Modbus-RTU/Modbus-RTU-1.0.52alpha.bin) | [download](../firmware/Modbus-RTU/Modbus-RTU-1.0.52alpha-merged.bin) |

## SHA-256 checksums

- `a8d8387be1312f6932997e58468b74e212f77594fbe9f92b85410c5ce30c8d43`  `OpenFumeExtractor-Master-1.9.35beta.bin`
- `75a7887ccfe82af8bac8b25989651e1c59473bee5652fb3432bc18e97e195959`  `OpenFumeExtractor-Master-1.9.35beta-merged.bin`
- `2c9960d083e14ddb7d5b91c65b5c1271b4a49b7a79367fa9118d3c53ab960df2`  `JBC-FAE-Bus-1.1.60beta.bin`
- `317701bc6b3d47dbe6c974f03d9a1817ab05d9c03bb7b35330c999a3f5e85794`  `JBC-FAE-Bus-1.1.60beta-merged.bin`
- `2925e149e620f35065ffc2b343856bca9178980b7bcc3de3099e47588588d44d`  `JBC-USB-1.1.77beta.bin`
- `0a59df3731ded6a0beb9ed9d2f5ad854db5e66c85aca5982b2180585ff33e866`  `JBC-USB-1.1.77beta-merged.bin`
- `b1ba793f2dd0325038c10f9e6902a3a4ec3bc658227f79a77a06b0d6d2d3cb2f`  `Fan-IO-1.1.54beta.bin`
- `37ca363aeb3e8f13662d8b939ec058f8bb625d661c0e8cf3b729b24fe67eb47e`  `Fan-IO-1.1.54beta-merged.bin`
- `7b47a2aa56dcf630cb74baaf74a4dba0b36744725e86809be3564a92d7bea6da`  `Fan-IO-Pro-1.1.41beta.bin`
- `a547c5713431acf7f253d4ca675fc7e9f8338ec024cea090dbda75c33f84f7ff`  `Fan-IO-Pro-1.1.41beta-merged.bin`
- `3d71cde8a98afb1874d14ac373067f43f8b0d6e89bdb702b1e573470d35ff13e`  `Weller-Zero-Smog-1.1.74beta.bin`
- `4205079ee895b82060d0856eb2bbbe72cd1291b6d386bf54bd0b70af6bb519c9`  `Weller-Zero-Smog-1.1.74beta-merged.bin`
- `ba2faee61935ac11808fe7f4012640706b68042ae61e399476a237ce8f79b8ec`  `Display-320x480-1.3.71beta.bin`
- `6f657e086911cfc2a7ba597229719e41285a1c8225e1a95f0ddda7683767096b`  `Display-320x480-1.3.71beta-merged.bin`
- `be8e1480ad288a19ed73a19661d1f5131244e796f14b9768e9117c9edbb84c92`  `Display-800x480-1.3.77beta.bin`
- `d4e1f7eb38c96106439136d6bb8900a645f45c2490c73a4f66d22918b84027e4`  `Display-800x480-1.3.77beta-merged.bin`
- `244fdd3e2f90c558b3b5f7e8d42e22ca64d8c1afcdaed4fd0945ae7c3f87704e`  `Universal-RS232-1.0.66alpha.bin`
- `0c35b9e235c7d6f07da47f1f7eab967d7b1180c728e6433187e2411fb6688c2b`  `Universal-RS232-1.0.66alpha-merged.bin`
- `13fcbde73316e22034ae0d0e7122cd5a70f56fe5a310f0564529c0434be917aa`  `Modbus-RTU-1.0.52alpha.bin`
- `287b89b1767abc542b6c09e9036b68671be92d90aba8c37bd3814c3ba51eb246`  `Modbus-RTU-1.0.52alpha-merged.bin`

## Update rules

- Never use a merged image in the web updater.
- Never flash an OTA package at address `0x0`.
- Display 320x480 and Display 800x480 are different firmware targets.
- The web updater rejects a file whose target or Ed25519 signature does not match the selected device.
