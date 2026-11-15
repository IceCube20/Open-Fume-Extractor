# Display Idle Delay And Small-Display Timing

Local source changes, not yet published or flashed by the agent:

| Display | Version |
|---|---|
| AXS15231B QSPI | 1.4.32RC |
| ST7796 SPI | 1.0.42RC |
| S3 RGB | 1.4.40RC |
| P4 RGB | 1.0.10beta |

## Automatic Backlight Delay

Previously, Only in power saving could keep an already expired idle timestamp
while normal mode temporarily restored the light. Once the master re-entered
Eco, often around a minute after extraction stopped, the light immediately
turned off again despite a configured one-hour delay.

All four displays now start a fresh configured delay when shutdown becomes
eligible. In Only in power saving this means entering Eco. Normal mode keeps
the light on without consuming the next Eco delay. In Always, Eco changes do
not restart the existing inactivity countdown. Off, touch wake, OTA/setup
inhibition, saved settings and two-touch screensaver wake remain unchanged.

Native tests cover the complete one-hour/off/extraction/Eco cycle, repeated
unchanged settings, Always mode, OTA inhibition and unsigned millis rollover.

## Small-Display Timer Adjustment

Both small ports already have a dedicated GUI task. Its five-millisecond
service cadence did not change LVGL's separate refresh and pointer-read timers,
which were still at the 33 ms library default. They are now explicitly set to
16 ms and the pointer is explicitly bound to the display.

No panel clock, transfer algorithm, framebuffer topology, touch-coordinate
mapping, scroll threshold, momentum, font, style or Home update cadence changed.
QSPI stays on the proven synchronous full-frame path at 60 MHz; ST7796 stays
on its direct dirty-rectangle SPI path at 80 MHz. The failed experimental
DMA/overlap paths are not re-enabled. No additional buffers are allocated.

This removes avoidable timer latency in lighter scenes; it does not guarantee
60 FPS or remove the rendering and transfer costs in the scrolling benchmark.
Compare the same page/gesture and benchmark after flashing, including CPU,
touch response, Home updates, pairing and OTA. On-device improvement is not
yet measured.

For a controlled baseline, compile with `OFE_SMALL_REFRESH_MS=33` and
`OFE_SMALL_TOUCH_MS=33`; the default is 16 for both. These are build definitions,
not saved user settings. Global LVGL files and the RGB timer settings are unchanged.
