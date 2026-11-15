# JBC USB Details on OFE Displays

Twelve-entry pages and the compact layout require Master **2.0.64RC** and an updated display:

| Display | Firmware |
| --- | --- |
| 320x480 | 1.4.21RC |
| 800x480 RGB (ESP32-S3) | 1.4.33RC |
| ST7796 320x480 | 1.0.31RC |
| ELECROW P4 800x480 | Included in the preparation sketch; hardware testing pending |

Open **Modules**, select the JBC USB module, and choose a detail category.
The display language controls labels. The header and back navigation stay visible.
Use the port selector for port-specific data and the arrow buttons for the next
or previous twelve entries. Each category contains only values supported by the
connected station and received by the Master.

Category/port selection and page arrows stay fixed while the value list scrolls.
The 800x480 layout uses two columns. On smaller displays, read-only values use
two compact columns; settings use one column to keep touch targets usable.
Long labels wrap instead of being cut off. The standard header remains visible.
Numeric settings open a centered compact keypad (360 px on small displays,
400 px on 800x480), rather than a full-width keyboard. The controls behind it
are blocked until confirmation or cancellation; the header stays visible.

## Categories

- **Station:** station name, model, family, JBC link, Work/Stand, connection mode,
  station error, software/hardware versions and device ID.
- **Ports:** tool, state, temperature, setpoint, power, airflow and countdowns;
  cleaner motor/door/mode, preheater thermocouples, extractor intake state or
  solder-feeder state according to the station class.
- **Counters:** all supported per-port lifetime and partial counters from the
  shared Home Assistant counter catalogue. Time counters use days/hours/minutes;
  cycle counts remain integers.
- **Settings:** the shared Home Assistant temperature, airflow and temperature-level
  controls, including level enable switches and the selected level. Unsupported
  controls are omitted, including temperature levels on the matching JTSE variant.
  Tap a numeric value for the keypad or use plus/minus. Changes are sent directly;
  no separate Save button is needed. The display waits for acceptance and then
  for the requested value to be read back before allowing another edit. A
  timeout/rejection is shown inline; acceptance alone is not shown as success.
- **Diagnostics:** module firmware/uptime/CPU, protocol and USB framing/transport
  counters. Heap and maximum-loop values require the Master's developer mode.

This covers the normal JBC USB entities and controls exposed to Home Assistant,
plus basic transport diagnostics. It is not a dump of every raw JBC DLL service,
robot, calibration, profile-array or other expert field from the web interface.
Station-name editing and destructive/reset operations remain in the web UI.

## Transport and Safety

RS485 and WiFi use the same versioned detail packets and command validation. An
older display continues to receive the existing compact B5/B6 telemetry; the
previous extended-detail firmware keeps its six-entry pages. The twelve-entry
window is explicitly negotiated, not inferred from resolution. A newer
display falls back to that compact view when an older/unreachable Master cannot
provide extended pages; update both sides to use the new categories.

Only the currently open category/port/window is requested. Refreshes stop during
the screensaver and OTA. At most twelve entries are cached, using approximately
1.6 KB of PSRAM plus LVGL widget storage. Controls are created only for rows that
need them, rather than allocating a switch, selector and buttons for every sensor.
The data is split over the existing bounded packets, not one oversized message.
Pixel clocks, draw buffers, touch
settings, Home refresh rates and OTA transport settings are unchanged.

Commands carry the target module, control ID and station revision. The Master
checks the active detail target, station identity, connection, current capability,
range and OTA state before forwarding a command. Repeated transport packets do not
repeat the physical write. A station change cancels a pending display edit/write.
These are application checks, not a replacement for trusted-network security.

Read-only data may remain cached when a station disconnects and is marked as such.
Unavailable values are shown as missing rather than invented zero readings.

## Verification

Run `tools/tests/display_hybrid_native/run_jbc_detail.ps1` for the real codec,
catalogue and Master handler regressions. Source checks are in
`tools/tests/test_display_jbc_detail_source.py`. Board compiler checks do not
replace testing on the actual panel and JBC station. Test both transports,
station swaps, level switches, numeric changes, theme changes, screensaver/wake
and OTA after flashing. No firmware is flashed by these tests.

`tools/tests/jbc_detail_ui/run.ps1` renders the actual shared UI with installed
LVGL and the display fonts on Windows. It checks row/control bounds and fixed
navigation at both logical resolutions, in dark/light themes, and emits PPM
images in a separate temporary build folder. It does not modify Arduino caches.
