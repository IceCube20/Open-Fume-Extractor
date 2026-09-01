# Hardware and Pinout

All GPIO signals are 3.3 V logic. The listed assignments match the released
firmware images.

## Master ESP32-S3

| Function | GPIO / connector |
|---|---:|
| OFE RS485 RX | 17 |
| OFE RS485 TX | 16 |
| two SK6812 GRBW status LEDs | 4 |
| Native USB | board USB-C connector |
| UART USB | board USB-C connector |

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

| Function | GPIO | Logic |
|---|---:|---|
| main output enable | 18 | active high |
| fan PWM | 19 | PWM |
| fan tachometer | 21 | two pulses/revolution |
| IN1 / IN2 | 32 / 33 | digital inputs |
| OUT1 / OUT2 | 22 / 23 | digital outputs |

The final fan power stage and protection circuit are application-specific and
are not defined by this firmware pinout.

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

## Mechanical envelope

The current cartridge concept uses the same **30 x 100 x 100 mm** enclosure size
for master and modules. Module USB and RS485 screw terminals remain accessible
from the rear. The master's two USB-C ports are accessible from the front.

