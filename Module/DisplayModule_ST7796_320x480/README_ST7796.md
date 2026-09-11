# OFE Display ST7796 320x480

Hardware fork of `DisplayModule_320x480`, based on the known-working ESPHome YAML supplied for this board.
The native ST7796 panel is 320x480 and the OFE UI runs in 480x320 landscape.

## Pinout

| Function | GPIO |
|---|---:|
| Backlight PWM | 9 |
| Display SCK | 10 |
| Display MOSI | 11 |
| Display CS | 12 |
| Display DC | 13 |
| Display RESET | 14 |
| Touch SCL | 3 |
| Touch SDA | 18 |
| Touch INT | 17 |
| Touch RESET | 8 |
| OFE RS485 TX | 15 |
| OFE RS485 RX | 7 |

GPIO16 is intentionally unused because it is connected to SD-CS on this display board.

## Display / touch

- ST7796, SPI mode 3, target SPI clock 80 MHz
- Arduino_GFX 1.6.6 or newer recommended
- RGB565 via Arduino_GFX
- ST7796 hardware rotation (`MADCTL`, rotation 1) exposes the OFE UI as 480x320
- LVGL uses direct dirty-rectangle transfers to the panel; there is no 320x480 full-frame Canvas refresh
- `invertDisplay(true)` matches the previous ESPHome `invert_colors: true` setting
- FT63x6 at I2C address 0x38, I2C 50 kHz
- Touch reproduces the old ESPHome order exactly: swap controller X/Y first, then calibrate YAML X=8..466 and Y=0..317, then mirror Y

### v30a hardware fixes

v30 incorrectly applied the YAML calibration limits to the controller axes before `swap_xy`. ESPHome swaps the raw axes first. Therefore the YAML X range 8..466 belongs to controller raw Y, while YAML Y 0..317 belongs to controller raw X. v30a fixes this and restores the full touch area.

v30 also inherited the AXS15231B full-frame Canvas strategy. A complete RGB565 frame is 307200 bytes, so even a small LVGL change caused a full-screen SPI transfer. v30a uses the ST7796 hardware rotation and sends only LVGL dirty rectangles directly to the panel. SPI remains at 80 MHz.

## OFE identity

- Addressing: automatic display range starting at `0x40` (same allocator as the other OFE displays)
- Capabilities include `CAP_DISPLAY_320X480` and dedicated `CAP_DISPLAY_ST7796`
- Firmware target: `DISPLAY_ST7796_320X480`
- Default firmware version: `1.0.7beta`

The dedicated target/capability prevents the regular AXS15231B 320x480 firmware from being accepted as a normal OTA image for this hardware.

## Arduino board settings

The source YAML used an ESP32-S3 board with 8 MB flash, octal PSRAM and 240 MHz CPU. For the OFE Arduino build use the matching ESP32-S3/OPI-PSRAM board settings for the actual board.

First hardware bring-up should verify the four touch corners. The calibration limits remain compile-time macros (`TOUCH_CAL_X_MIN/MAX`, `TOUCH_CAL_Y_MIN/MAX`) so a panel revision can be adjusted without changing the UI code.

## v30c picture tuning

The perceptual backlight curve from v30b is retained. The extra ESPHome-derived
B0/B4/B6/B7/C5 panel-register override was removed because it changed the scan/
entry behavior on the real panel and resulted in a 180-degree wrong presentation.
The stable Arduino_GFX ST7796 initialization, 80 MHz SPI, hardware rotation and
v30a partial-refresh path are used again. Future color tuning should be done
incrementally (gamma/VCOM only) after the stable baseline is confirmed.

## v30e boot presentation

- `Satt` / `Vivid` is the default LCD color profile for a fresh installation, based on real-panel testing.
- The `LCD-Farben` / `LCD color` selector remains available and persistent with Standard, Kontrast/Contrast and Satt/Vivid.
- Backlight GPIO9 is forced off at the first application instruction and stays off through ST7796 initialization, hardware rotation, inversion, saved color-profile loading and the first forced LVGL refresh.
- The saved brightness is only applied after a valid LVGL boot frame is on the panel. This prevents the undefined white-noise / briefly inverted GRAM contents from becoming visible during normal application startup.
- SPI remains 80 MHz; partial refresh, touch mapping, RS485 pins and automatic address allocation are unchanged.

## v30f display polish

- DHCP hostname: `OFE-ST7796-320x480-XXXXXX`, where `XXXXXX` is the last 6 hexadecimal digits of the ESP32 base/WLAN MAC.
- The hostname is set before STA mode starts so DHCP receives the custom name.
- `Booting...` is drawn after rotation, inversion, saved `LCD-Farben` profile and brightness are valid; only then is GPIO9 backlight enabled.
- Screensaver brightness cap is raised from 18% to 30%.


## v30g ST7796 brightness scale

- ST7796 only: remap the visible 10..100% brightness range onto the previous 30..100% range.
- New 10% therefore matches the panel brightness that v30f showed at 30%; 100% remains unchanged.
- The existing perceptual PWM curve stays underneath the remap.
- ST7796 screensaver brightness cap raised from 30% to 50% of the new scale.
- DisplayModule_320x480 and DisplayModule_800x480 brightness behavior remains unchanged.
