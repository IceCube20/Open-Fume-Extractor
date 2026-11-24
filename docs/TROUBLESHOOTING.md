# Troubleshooting

## Master does not boot after flashing

- confirm that the exact target's `*-merged.bin` was written at address `0x0`
- erase the complete flash when switching partition layouts
- verify the chip, board revision and 4/16/32 MB flash target; S3, P4 PRE-V3 and PoE V3 images are different
- inspect the 115200-baud serial log

`No bootable app partitions` normally means the partition table and application
image do not belong together or the application was not written to the expected
slot. Reflash the matching merged image.

## Module is missing

1. Check A/B polarity and common ground.
2. Check 250000 baud OFE firmware and stable supply.
3. Scan modules in Bus Diagnostics.
4. Run address assignment if two modules used the same factory address.
5. Fit 120-ohm termination only at both physical bus ends.

## Module is online but its attached device is offline

The OFE bus and local device bus are independent. Check the module details and
local trace. For Universal RS232 confirm voltage level, baud, frame, checksum and
poll/match pattern. For Modbus confirm slave ID, register address and function.

## Home Assistant shows stale entities

- ensure MQTT is connected and the master availability topic is online
- enable **Home Assistant Discovery** under **Network/System > MQTT** and save;
  live values can keep updating while Discovery is disabled, but new devices and
  entity removals will not be published
- allow Discovery reconciliation to complete after changing module/profile type
- verify that the module returned its new descriptor
- restart Home Assistant only after the master has published entity removals

With Master 2.0.53 and native integration 0.1.0rc21, two confirmed reports of no
station connected to an online JBC USB module remove its station-specific
entities. The module itself and its connection/Work/Stand indicators remain.
An offline OFE module is deliberately preserved. For the native integration,
replace the component folder and restart HA when updating; removing the
integration is not necessary.

## Yellow MCU EVT but No Alarm in the Web UI

On MCU versions before 2.1.52, the LED mapping can still treat a muted optional
JBC/Weller disconnect as a warning. Update to the matching 2.1.52RC MCU image;
physical and virtual EVT then follow the same module alarm policy. Refresh the
page after the MCU restarts. The fix does not require changing the HA integration.

Do not disable real alarms just to clear an LED. An offline OFE node, a faulty
required output, an active filter warning or missing main signal selection may
legitimately keep an indication active. Check the alarm centre and the module's
Connection monitoring preference separately from its current main-output role.

## HA LED Colors Update but Effects Are Static

First compare another client. The OFE cards intentionally respect
`prefers-reduced-motion`: animation is disabled when the browser requests it,
while colors/states remain visible. On Windows 11, check Settings > Accessibility >
Visual effects > Animation effects and reload the page. Do not change this
accessibility preference if the user needs reduced motion. Check the actual
OFE card rather than expecting ordinary HA sensor rows to animate.

If all clients are affected, verify that the LED sensor attributes still contain
`ofe_led_effect`, `ofe_led_period_ms` and `ofe_led_enabled`, and that the matching
card resource was loaded. Do not increase the MCU polling rate to create
animation frames: the card renders the effect locally.

## P4 USB Stick Is Detected but Not Ready

A root link/device counter is not proof of a mounted filesystem. Check the
physical host port/jumper and adequate power, then distinguish enumeration from
mass-storage installation and mounting in the developer diagnostics/serial log.
Try another port or direct connection and a known-good backed-up FAT32 stick
before reformatting. See [USB storage](USB.md). Do not unplug during a write.

## C6 Version Is Missing or an Update Is Blocked

A dash may mean the installed firmware cannot answer the version RPC; it does
not alone prove that the chip is absent. Collect the C6/Hosted/RPC log and
check the exact board profile, SDIO wiring and running MCU SDK. Working Ethernet
does not prove working C6 transport. WiFi scan and version RPC are separate tests.

C6 installation requires stable Ethernet, an idle radio/update path, exact image
validation and a separately confirmed recovery path. Normal downgrades remain
blocked. Private-developer version testing can explicitly allow a known target
change, but cannot recover every failed SDIO image. Legacy SDIO confirmation is
not a generic bypass. Use the [C6 guide](Hosted-C6-Catalog.md) and exact board's
recovery instructions; an unresponsive C6 may need direct UART access.

## Designer Cannot Save or a Wire Takes an Unexpected Path

Read the reported block/connection/output-target or 24,000-byte limit before
removing data. The stored definition remains unchanged after a rejected save.
Manual wire routes also use graph bytes. Check locked segments; Unlock releases
a manual path and Reroute recalculates automatic wires. A dashed visual route is
not a changed logical connection. If WebAssembly cannot load, inspect the routing
state shown by the toolbar and use the authenticated MCU page, not a copied
standalone HTML file. See [designer details](System-Automation.md).

## Display remains on Booting

Inspect the serial log for PSRAM or LVGL allocation failures. Confirm the exact
display model and firmware. The AXS15231B 320x480, ST7796 320x480 and 800x480
images are not interchangeable.

## 800x480 display artifacts

Use the released merged/OTA image built for `Display-800x480`. Do not substitute a
generic ESP32-S3 build. Also verify a stable 5 V supply and keep display wiring and
the board unmodified.

## Firmware update is rejected

- target and firmware type must match
- the OTA `.bin` must contain a valid OFE Ed25519 authentication trailer
- do not upload a merged image through the web updater
- compare the file SHA-256 with [FIRMWARE.md](FIRMWARE.md)

## Update was cancelled and module remains busy

Retry the abort action from the update page. If communication was lost during the
abort, power-cycle the module. If it no longer boots, restore it locally with the
matching merged image.
