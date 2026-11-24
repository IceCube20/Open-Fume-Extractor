# Hardware and Pinout

All GPIO signals are 3.3 V logic. The listed assignments match the released
firmware images for release 2.1.52RC. GPIO numbers are chip signal numbers,
not physical header positions. Existing saved Fan/IO hardware configuration can
replace the defaults below. Always check the exact carrier and revision.
See [German pinout](Hardware-Pinbelegung.md) for the corresponding German guide.

## MCU-S3

| Function | GPIO / connector |
|---|---:|
| OFE RS485 RX | 17 |
| OFE RS485 TX | 16 |
| two SK6812 GRBW status LEDs | 4 |
| Native USB | board USB-C connector |
| UART USB | board USB-C connector |

## MCU-P4: Waveshare PRE-V3 and PoE V3.1+

These two MCU targets use the following OFE application connections, but require
separate firmware images. The PoE profile supports the tested V3.2 board; it is
not interchangeable with the PRE-V3 Module DEV KIT or the ELECROW P4 display.

| Function | P4 GPIO / connection | Notes |
|---|---:|---|
| OFE RS485 RX | 21 | transceiver RO -> MCU RX |
| OFE RS485 TX | 20 | MCU TX -> transceiver DI |
| two external SK6812 GRBW status LEDs | 4 | not the carrier's power LED |
| C6 SDIO CLK / CMD | 18 / 19 | onboard, reserved |
| C6 SDIO D0 / D1 / D2 / D3 | 14 / 15 / 16 / 17 | onboard, reserved |
| C6 reset | 54 | onboard, reserved |
| Ethernet PHY | IP101, address 1 | onboard RMII |
| Ethernet MDC / MDIO / reset | 31 / 52 / 51 | onboard, reserved |
| USB storage | native USB host connector | not the USB-UART flashing connector |

Use a 3.3 V-compatible UART-to-RS485 transceiver; A/B must never connect directly
to GPIO21/20. The supplied OFE bus uses automatic direction switching, with no
separate DE/RE GPIO. Run only one MCU on an OFE RS485 segment.

The board configuration additionally reserves GPIO14..19, 28..31, 34..38 and
49..54. On PoE it also reserves GPIO6, 7..13 and 39..45 for carrier wiring.
An unlisted GPIO is not automatically safe or available on a header. Keep
Ethernet, radio, storage and other onboard wiring intact when assigning pins.
See [PRE-V3 board guide](Master-P4-Waveshare.md) and
[PoE V3 board guide](MCU-P4-PoE-V3.md).

## ESP32 DevKit module base

Used by JBC FAE Bus, Weller, Fan/IO, Fan/IO Pro, Universal RS232 and Modbus RTU.

| Function | GPIO |
|---|---:|
| OFE RS485 RX | 26 |
| OFE RS485 TX | 25 |
| two SK6812 GRBW status LEDs | 4 |

### JBC FAE Bus

| Function | GPIO |
|---|---:|
| JBC RX | 17 |
| JBC TX | 16 |

### Weller Zero Smog

| Function | GPIO | Setting |
|---|---:|---|
| Weller RX | 17 | 1200 baud |
| Weller TX | 16 | 1200 baud |

Use a suitable MAX3232 interface for a physical RS232 connection.

### Universal RS232

| Function | GPIO | Default |
|---|---:|---|
| local RX | 17 | 9600 baud, 8N1 |
| local TX | 16 | 9600 baud, 8N1 |

The loaded profile can override baud rate, frame format, protocol, checksum and
line ending. Use a MAX3232 for RS232 voltage levels.

### Modbus RTU

| Function | GPIO | Default |
|---|---:|---|
| local Modbus RX | 17 | 9600 baud, 8N1 |
| local Modbus TX | 16 | 9600 baud, 8N1 |

The local Modbus bus needs its own RS485 transceiver and is electrically separate
from the OFE bus.

### Fan/IO and Fan/IO Pro

These are factory/default assignments, not fixed wiring for every saved profile.
The hardware editor and the module both validate reserved pins and collisions.

| Function | GPIO | Logic |
|---|---:|---|
| main output enable | 18 | active high |
| fan PWM | 19 | PWM |
| fan tachometer | 21 | two pulses/revolution |
| IN1 / IN2 | 32 / 33 | digital inputs |
| OUT1 / OUT2 | 22 / 23 | digital outputs |

Additional configurable functions:

| Function | GPIO assignment | Notes |
|---|---|---|
| addressable WS2812/SK6812-style strip | free output-capable GPIO from the editor's pin list | disabled by default; one strip, up to 128 pixels / 32 virtual channels |
| Fan/IO Pro analog pressure input | configured input GPIO | no sensor pin assigned by default |
| Fan/IO Pro SDP3x pressure sensor | configured SDA/SCL GPIOs | no sensor pins assigned by default |

The virtual strip does not reuse the two OFE/EVT status LEDs on GPIO4. Never
assign its data signal to GPIO4, OFE RX/TX, a reserved pin or an already active
input/output/fan/sensor pin. It needs suitable power and level interfacing, not
power from a GPIO. See [LED output setup](FANIO_LED_OUTPUTS.md).

The final fan power stage and protection circuit are application-specific and
are not defined by this firmware pinout.

Important ESP32 GPIO limitation: GPIO34 through GPIO39 are input-only and have
no internal pull-up or pull-down resistors. When one of these pins is selected as
a configurable Fan/IO input, fit an external pull resistor so the disconnected
state cannot float. The hardware editor must not assign these pins to outputs.

## JBC USB ESP32-S3

| Function | GPIO / connection |
|---|---:|
| OFE RS485 RX / TX | 17 / 16 |
| two SK6812 status LEDs | 4 |
| native USB D- / D+ | 19 / 20 |
| USB VBUS | protected, current-limited 5 V |

The module acts as USB host for the station's CP210x adapter. The UART behind
CP210x uses 500000 baud, 8E1.

## Display 320x480

Target: JC3248W535C_I_Y with AXS15231B/QSPI.

| Function | GPIO |
|---|---:|
| OFE RS485 RX / TX | 18 / 17 |
| status LED | 46 |
| backlight | 1 |
| touch SDA / SCL | 4 / 8 |
| touch RST / INT | 12 / 11 |
| display CS / SCK | 45 / 47 |
| display MOSI / MISO | 21 / 48 |
| display Quad WP / HD | 40 / 39 |

## Display 800x480

Target: Guition JC8048W550 with RGB565 panel and GT911 touch.

| Function | GPIO |
|---|---:|
| OFE RS485 RX / TX | 18 / 17 |
| backlight | 2 |
| touch SDA / SCL | 19 / 20 |
| touch RST | 38 |
| DE / VSYNC / HSYNC / PCLK | 40 / 41 / 39 / 42 |
| Red R0..R4 | 45, 48, 47, 21, 14 |
| Green G0..G5 | 5, 6, 7, 15, 16, 4 |
| Blue B0..B4 | 8, 3, 46, 9, 1 |

## Display ST7796 320x480

Target: ST7796 SPI panel with FT63x6 touch.

| Function | GPIO |
|---|---:|
| OFE RS485 RX / TX | 7 / 15 |
| optional external status LED | 46 (disabled by default) |
| backlight PWM | 9 |
| display SCK / MOSI | 10 / 11 |
| display CS / DC / RESET | 12 / 13 / 14 |
| touch SDA / SCL | 18 / 3 |
| touch INT / RESET | 17 / 8 |

GPIO16 remains unused because it is connected to SD-CS on this display board.

## Display P4 800x480

Target: ELECROW CrowPanel Advanced 5-inch V1.0, RGB/ST7262 + GT911,
with the PRE-V3 ESP32-P4 build profile. These are onboard connections, not spare
GPIOs for external devices. Check your exact board revision before wiring.

| Function | GPIO / connection |
|---|---:|
| OFE RS485 UART RX / TX | 48 / 47 |
| shared I2C SDA / SCL | 45 / 46 |
| touch RESET / INT | 36 / 42 |
| DE / VSYNC / HSYNC / PCLK | 2 / 41 / 40 / 3 |
| RGB data, driver D0..D15 order | 8, 7, 6, 5, 4, 14, 13, 12, 11, 10, 9, 19, 18, 17, 16, 15 |
| backlight controller | STC8, I2C `0x2F`; no direct PWM GPIO |
| onboard C6 SDIO CLK / CMD | 53 / 54 |
| onboard C6 SDIO D0 / D1 / D2 / D3 | 52 / 51 / 50 / 49 |
| onboard C6 reset | 20 |

No external status-LED GPIO is assigned by default on this P4 display
(`OFE_STATUS_LED_PIN = -1`). Do not interpret a virtual OFE/EVT indication as a
spare physical LED pin.

The C6 signals are reserved onboard wiring. They differ from the Waveshare MCU
radio pins above. Do not apply a Waveshare MCU pinout to this display.

The board UART connector is not an RS485 transceiver. Use a suitable 3.3 V UART
to RS485 interface and follow its direction-control and electrical requirements.
Do not connect RS485 A/B directly to MCU GPIOs.

## Mechanical envelope

The current cartridge concept uses the same **30 x 100 x 100 mm** enclosure size
for MCU and modules. Module USB and RS485 screw terminals remain accessible
from the rear. The MCU-S3's two USB-C ports are accessible from the front. P4 carrier connector
positions differ; this mechanical concept does not specify their enclosure layout.
