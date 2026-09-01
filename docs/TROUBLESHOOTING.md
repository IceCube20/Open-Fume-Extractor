# Troubleshooting

## Master does not boot after flashing

- confirm that the exact target's `*-merged.bin` was written at address `0x0`
- erase the complete flash when switching partition layouts
- verify 4 MB versus 16 MB target selection
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
- allow Discovery reconciliation to complete after changing module/profile type
- verify that the module returned its new descriptor
- restart Home Assistant only after the master has published entity removals

## Display remains on Booting

Inspect the serial log for PSRAM or LVGL allocation failures. Confirm the exact
display model and firmware. The 320x480 and 800x480 images are not interchangeable.

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

