# Third-Party Notices

Open Fume Extractor uses the following third-party software. The project license
does not replace the licenses listed here.

| Component | Tested version | License | Use |
|---|---:|---|---|
| [Arduino-ESP32](https://github.com/espressif/arduino-esp32) | 3.3.12 for MCU builds; 3.2.0 for the high-performance display build | LGPL-2.1-or-later | ESP32 Arduino core |
| [ESP-IDF](https://github.com/espressif/esp-idf) | Bundled with the selected Arduino-ESP32 package | Apache-2.0 and component-specific licenses | ESP32 runtime, networking, USB and drivers |
| [Adafruit NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel) | 1.15.5 | LGPL-3.0-or-later | SK6812 status LEDs |
| [LVGL](https://github.com/lvgl/lvgl) | 9.6.0 | MIT | Display user interface |
| [Arduino_GFX](https://github.com/moononournation/Arduino_GFX) | 1.6.9 | BSD-3-Clause | Display transport and panel support |
| [PubSubClient3](https://github.com/hmueller01/pubsubclient3) | 3.3.0 | MIT | MQTT client |
| [libsodium](https://github.com/jedisct1/libsodium) | ESP32 precompiled component | ISC | Ed25519 firmware verification |
| [ESP Web Tools](https://github.com/esphome/esp-web-tools) | 10.x, loaded from unpkg | Apache-2.0 | Browser USB flasher |
| [Montserrat](https://github.com/JulietaUla/Montserrat) | generated bitmap subsets | SIL Open Font License 1.1 | Display fonts |
| [Material Design Icons](https://github.com/Templarian/MaterialDesign) | serial-port icon path | Apache-2.0 | Display connection icon |
| [libavoid-js](https://github.com/Aksem/libavoid-js/tree/28a6fe20c2f1f809afdbeca8d789e65357614c4f) / libavoid | 0.4.5 / adaptagrams v1.0.4 | LGPL-2.1-or-later | Boolean Designer browser wire routing |

Copies of the principal license texts are provided in `LICENSES/`. ESP-IDF and
its precompiled libraries contain additional notices in the corresponding
Espressif source distributions. The pinned download URLs and checksums for the
special 800x480 display build are recorded in
`tools/build_display_800x480_high_perf.ps1`.

No proprietary JBC libraries, firmware or documentation are included.

The unmodified libavoid JavaScript/WebAssembly modules, complete license,
corresponding-source links and replacement/build instructions are in
`assets/vendor/libavoid/`. They execute as separate browser modules, not as
MCU machine code. `tools/logic/build_router_assets.py` verifies pinned upstream
Git blobs and regenerates the compressed firmware assets offline.

