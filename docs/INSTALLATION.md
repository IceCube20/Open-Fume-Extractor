# Installation

## Required hardware

- ESP32-S3 master with 16 MB flash and 8 MB OPI PSRAM
- at least one compatible OFE module
- automatic-direction TTL-to-RS485 transceiver for each OFE node
- stable 5 V supply with adequate current reserve
- a USB flashing tool capable of writing a merged ESP32 image at address `0x0`

See [PINOUT.md](PINOUT.md) for GPIO assignments.

## Firmware file types

Each target has two files:

| File | Purpose |
|---|---|
| `*-merged.bin` | complete first flash: bootloader, partition table and application |
| normal `*.bin` | cryptographically signed OTA package for the master update page |

Do not use the OTA file as a complete address-`0x0` image. Do not upload a merged
file through the web updater.

## First flash

The recommended method is the [GitHub Pages web flasher](../). Open it in a
current desktop version of Chrome or Edge, select the exact target and connect
the device over USB. See [GITHUB-PAGES.md](GITHUB-PAGES.md) when publishing a
new repository.

1. Disconnect the target from the OFE bus and attached equipment.
2. Select the exact merged image for the target.
3. Erase the complete flash when changing firmware family or partition layout.
4. Write the merged image to flash address `0x0`.
5. Reset the board and monitor its serial output at 115200 baud.
6. Reconnect the OFE bus only after the firmware starts correctly.

Expected flash sizes:

- Master, JBC USB and both displays: **16 MB** targets
- JBC FAE Bus, Fan/IO, Fan/IO Pro, Weller, Universal RS232 and Modbus RTU:
  **4 MB** ESP32 targets

Writing a 16 MB merged image to a 4 MB ESP32, or the reverse, is the wrong target
selection and must be avoided.

## OFE RS485 bus

The OFE bus uses **250000 baud, 8N1**. Connect A to A, B to B and GND to GND.
Build a linear bus and avoid long stubs. Fit **120 ohm termination at the two
physical ends only**.

Address families:

| Range | Module family |
|---|---|
| `0x10`-`0x1F` | JBC FAE Bus and JBC USB |
| `0x20`-`0x2F` | Fan/IO and Fan/IO Pro |
| `0x30`-`0x3F` | Weller Zero Smog |
| `0x40`-`0x4F` | Displays |
| `0x50`-`0x5F` | Universal RS232 |
| `0x60`-`0x6F` | Modbus RTU |

## Master commissioning

1. Open the IP address printed by the serial console or use the master setup
   access point when no WiFi credentials are stored.
2. Configure WiFi, web login and optionally MQTT under **Network Setup**.
3. Open **Bus Diagnostics**, scan for modules and run address assignment if needed.
4. Select the main extraction input and output on the status page.
5. Configure extraction level, work/stand after-run and optional after-run power.
6. Download a backup after the basic setup has been verified.

Useful serial CLI commands:

```text
help
status
modules
module 0x50
routes
bus
network
mqtt
scan
```

## Acceptance test

- every installed module appears online with the expected address and firmware
- no consecutive live misses are increasing
- main input starts the selected output
- after-run duration and after-run power work as configured
- web UI and display show the same alarms and values
- Home Assistant entities become available when MQTT is enabled
- settings survive a master restart
- backup export and restore have been tested
