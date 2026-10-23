# Display Connection: RS485 and WiFi

All four display variants can communicate with the master through the OFE RS485 bus
or through an authenticated WiFi pairing.

## Failover behavior

- RS485 is used while the wired link is available.
- If RS485 is lost and valid WiFi settings are stored, the display reconnects to
  the master wirelessly.
- The header icon shows the active serial or WiFi transport.
- The master keeps a paired WiFi display online without generating unnecessary
  RS485 offline events.

## Pairing while RS485 is connected

1. Start the display on the OFE bus.
2. Open display pairing under the master's Network Setup page.
3. Show the pairing code for the display serial number.
4. On the display open **System > Connection: RS485 / WiFi**.
5. Import the network configuration and enter the pairing code.
6. Disconnect RS485 temporarily and verify that the WiFi icon appears.

## Pairing without RS485

For direct Bluetooth setup:

1. Open the display's connection page and start Bluetooth pairing.
2. In the master's header select Bluetooth, then **Find displays**.
3. Select the matching display serial number from the results.
4. Compare the six-digit code and confirm it on **both** devices.
5. Allow the display to save the network settings and connect over WiFi.

Discovery continues while searching, including displays entering pairing mode
after the search began. The master shows search elapsed time. Bluetooth is used
for provisioning, not for continuous display traffic. Internal RAM use increases
temporarily while the radio's Bluetooth stack is active; application caches use
PSRAM where suitable. Not all controller memory can be moved to PSRAM.

SSID, WiFi password, master address and pairing code can be entered manually on
the display System page. This permits a completely wireless first connection.
Saved pairing can be removed on the display connection page.

## Screensaver And Backlight

New display firmware exposes automatic backlight shutdown under the display
settings in the master, MQTT and native HA. Choose **Off**, **Only in power
saving** or **Always**, then select a delay from **1 to 60 minutes**. These
controls are hidden for firmware without support.

If the backlight is off, the first touch only restores lighting. Release the
screen and touch again to leave the screensaver; the waking touch does not
accidentally operate a hidden control. Normal brightness and screensaver dimming
remain separate settings.

## Wireless firmware updates

A paired display can receive its own update over WiFi. It also receives progress
events when another module is being updated, including transfer speed and the
five-second completion state before returning to the previously open page.

## Display 320x480

The small display uses an AXS15231B QSPI panel, a full canvas and software rotation.
It uses the standard display firmware image in the release package.
The tested profile uses B32, 60 MHz and full-frame transfers. Partial updates
are not enabled; the slower experimental overlapped path is also disabled.

## Display ST7796 320x480

This variant uses an ST7796 SPI panel with FT63x6 touch. It uses hardware rotation
and transfers LVGL dirty rectangles directly to the panel. Select the dedicated
`Display-ST7796-320x480` firmware; it is not interchangeable with the AXS15231B
320x480 image.
The released build keeps the working direct SPI path at 80 MHz, not the
experimental asynchronous DMA adapter.

## Display 800x480

The Guition JC8048W550 uses a timing-sensitive parallel RGB565 panel. Always use
the dedicated `Display-800x480` release files. The released firmware is built with
the verified high-performance SDK configuration, 16 MHz PCLK, a 64-byte data-cache
line, PSRAM XIP and the tested VSYNC/cache synchronization.

## Display P4 800x480 (Experimental)

The new `Display-P4-800x480` target is for the ELECROW CrowPanel Advanced
5-inch 800x480 RGB/ST7262 + GT911 board, using the **PRE-V3** chip/SDK profile.
Networking and Bluetooth use its ESP-Hosted C6 co-processor. The current
owner-tested profile uses 24 MHz PCLK and a dedicated GUI task.

This image is **1.0.7alpha-P4Prep**, not an S3 image or a generic P4 firmware.
It requires a current P4-aware master. Do not flash it on an incompatible board
or chip revision. The high-performance SDK remains separate from this repository;
see [Building](BUILDING.md) for the pinned preparation tools.

## Performance Measurements

The owner measured about 20 FPS on S3 RGB and 41 FPS on P4 in the OFE scrolling
benchmark. The small QSPI/SPI targets measured about 15/16 FPS. Resolution,
rendering and transfer paths differ; these figures are not interchangeable
full-screen refresh guarantees. A stationary screen can correctly report 0 FPS.
