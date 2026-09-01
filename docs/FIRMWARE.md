# Firmware Files

Install a complete merged image with the [GitHub Pages web flasher](https://icecube20.github.io/Open-Fume-Extractor/).

Release tree generated: **2026-09-02**

The normal `.bin` files are Ed25519-signed OTA packages for the master web updater.
The `*-merged.bin` files are complete images for an initial USB flash at address `0x0`.

| Target | Version | Signature target | OTA file | Merged file |
|---|---:|---|---|---|
| OpenFumeExtractor-Master | `1.9.23beta` | `MASTER` | [download](../firmware/OpenFumeExtractor-Master/OpenFumeExtractor-Master-1.9.23beta.bin) | [download](../firmware/OpenFumeExtractor-Master/OpenFumeExtractor-Master-1.9.23beta-merged.bin) |
| JBC-FAE-Bus | `1.1.59beta` | `JBC_BUS` | [download](../firmware/JBC-FAE-Bus/JBC-FAE-Bus-1.1.59beta.bin) | [download](../firmware/JBC-FAE-Bus/JBC-FAE-Bus-1.1.59beta-merged.bin) |
| JBC-USB | `1.1.75beta` | `JBC_USB` | [download](../firmware/JBC-USB/JBC-USB-1.1.75beta.bin) | [download](../firmware/JBC-USB/JBC-USB-1.1.75beta-merged.bin) |
| Fan-IO | `1.1.53beta` | `FAN_IO` | [download](../firmware/Fan-IO/Fan-IO-1.1.53beta.bin) | [download](../firmware/Fan-IO/Fan-IO-1.1.53beta-merged.bin) |
| Fan-IO-Pro | `1.1.40beta` | `FAN_IO_PRO` | [download](../firmware/Fan-IO-Pro/Fan-IO-Pro-1.1.40beta.bin) | [download](../firmware/Fan-IO-Pro/Fan-IO-Pro-1.1.40beta-merged.bin) |
| Weller-Zero-Smog | `1.1.73beta` | `WELLER_ZERO_SMOG` | [download](../firmware/Weller-Zero-Smog/Weller-Zero-Smog-1.1.73beta.bin) | [download](../firmware/Weller-Zero-Smog/Weller-Zero-Smog-1.1.73beta-merged.bin) |
| Display-320x480 | `1.3.57beta` | `DISPLAY_320X480` | [download](../firmware/Display-320x480/Display-320x480-1.3.57beta.bin) | [download](../firmware/Display-320x480/Display-320x480-1.3.57beta-merged.bin) |
| Display-800x480 | `1.3.64beta` | `DISPLAY_800X480` | [download](../firmware/Display-800x480/Display-800x480-1.3.64beta.bin) | [download](../firmware/Display-800x480/Display-800x480-1.3.64beta-merged.bin) |
| Universal-RS232 | `1.0.65alpha` | `UNIVERSAL_RS232` | [download](../firmware/Universal-RS232/Universal-RS232-1.0.65alpha.bin) | [download](../firmware/Universal-RS232/Universal-RS232-1.0.65alpha-merged.bin) |
| Modbus-RTU | `1.0.51alpha` | `MODBUS_RTU` | [download](../firmware/Modbus-RTU/Modbus-RTU-1.0.51alpha.bin) | [download](../firmware/Modbus-RTU/Modbus-RTU-1.0.51alpha-merged.bin) |

## SHA-256 checksums

- `0d578875fb86fb71ef8e135cac2af29c9f3d63dcdc0c572fd5699680a30d4a0b`  `OpenFumeExtractor-Master-1.9.23beta.bin`
- `8a9caf7d9afb60fc0dc46bb78147cfdbcd53148da907ea7abc52d4f0c87321bb`  `OpenFumeExtractor-Master-1.9.23beta-merged.bin`
- `12df6067f8cba44690ce1cb98328c0f4d70a53383c336cb2bc3c3f7f27dd139a`  `JBC-FAE-Bus-1.1.59beta.bin`
- `e1a86075db172feb81583b5ba3c8376964fa9ca1acbbb92f061d5762e9edecfb`  `JBC-FAE-Bus-1.1.59beta-merged.bin`
- `79771ec4a833e1aa6589199ff3f5fc434e72d984951aec4e57d1a7ba6458259e`  `JBC-USB-1.1.75beta.bin`
- `0ef1047418720d6df24c04e169d1aed07e8bcf65d9e2af0d83162b6cef248e00`  `JBC-USB-1.1.75beta-merged.bin`
- `117fe11c6ae8bf9af130f913a91b5291d0b5109aa44d1213e71773239adb1f53`  `Fan-IO-1.1.53beta.bin`
- `241992f29f63c5bef49b132b8322a9589b00b4e0fcc9604e0816b3222423c14e`  `Fan-IO-1.1.53beta-merged.bin`
- `9be5098b4290e6f7ae75e63ad3816d744ffed912f1d39cb9d687261b1380e22a`  `Fan-IO-Pro-1.1.40beta.bin`
- `7fc68156caacd12a4e05cc82aa0a78eee939510371477b5297ec6a1482d17185`  `Fan-IO-Pro-1.1.40beta-merged.bin`
- `384f1b373296317671d863fa1c519e0de95b5f62f1171b4a6ed3ca3fff133f30`  `Weller-Zero-Smog-1.1.73beta.bin`
- `13374fe2acd47508a001f092a34677017f67bbb3854f500b198166ae78fad366`  `Weller-Zero-Smog-1.1.73beta-merged.bin`
- `207351994e9059be037d3cee88ecd08e28313dcc2d8169740822ae2a8674cf3d`  `Display-320x480-1.3.57beta.bin`
- `118b522f104761cacf149ad053c0fab0417e57f61df18d5e6b4d4415646ec0f4`  `Display-320x480-1.3.57beta-merged.bin`
- `8948f484c9f989ecaf31caf2254bbad405bc794326ad1792a811ce37dbbe36c6`  `Display-800x480-1.3.64beta.bin`
- `412fb999da1ee706851d8d5184ce3c554470f934afaaebdf2ae20f21b0b114e3`  `Display-800x480-1.3.64beta-merged.bin`
- `2ebad7e0d8650d2c00369c809ba409467ca9fbd8d9d2c35536a1037511475e63`  `Universal-RS232-1.0.65alpha.bin`
- `7d7c66796dcb3e7b428d277957690f17a615b91adc1283d8e6946d87ef410b3a`  `Universal-RS232-1.0.65alpha-merged.bin`
- `256ffefbf0c421a2dacb82f08b6bac8e588058795c5b561723f7b225489bfbf0`  `Modbus-RTU-1.0.51alpha.bin`
- `2d69e433500a37295cd543ab927f7ff1007180f13b186a96898d3de7a3fd6f50`  `Modbus-RTU-1.0.51alpha-merged.bin`

## Update rules

- Never use a merged image in the web updater.
- Never flash an OTA package at address `0x0`.
- Display 320x480 and Display 800x480 are different firmware targets.
- The web updater rejects a file whose target or Ed25519 signature does not match the selected device.
