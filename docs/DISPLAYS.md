# Display Connection: RS485 and WiFi

Both display variants can communicate with the master through the OFE RS485 bus
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

SSID, WiFi password, master address and pairing code can be entered manually on
the display System page. This permits a completely wireless first connection.

## Wireless firmware updates

A paired display can receive its own update over WiFi. It also receives progress
events when another module is being updated, including transfer speed and the
five-second completion state before returning to the previously open page.

## Display 320x480

The small display uses an AXS15231B QSPI panel, a full canvas and software rotation.
It uses the standard display firmware image in the release package.

## Display 800x480

The Guition JC8048W550 uses a timing-sensitive parallel RGB565 panel. Always use
the dedicated `Display-800x480` release files. The released firmware is built with
the verified high-performance SDK configuration, 16 MHz PCLK, a 64-byte data-cache
line, PSRAM XIP and the tested VSYNC/cache synchronization.

