# Master CPU Optimization: 2.0.92 RC

## Observed Baseline

User-provided 2.0.91RC developer snapshots show about 49-51% total CPU,
22-25% on core 0 and 76-77% on core 1. Task samples assign roughly 32-34%
of one core to the control loop, 23-26% to HTTP/WiFi and 18% to MQTT.
These tasks share core 1; the dual-core average understates that core's load.
Loop-section timings include waiting/preemption and are not CPU percentages.

## Changes

- Encode JSON strings in contiguous runs rather than one Arduino String append
  per byte. MQTT and HTTP use the same escaping rules: quotes, backslashes,
  newline, carriage return and tab are escaped; other control bytes become a
  space. UTF-8 bytes are preserved.
- Append 38 live-state text fields directly to the existing PSRAM buffer,
  avoiding intermediate escaped Strings. The generic descriptor-definition
  writer remains compatible with both String and PSRAM outputs.
- Retain the MQTT module-state String's allocated capacity between publish
  cycles, clearing its length before every module. This buffer is used only by
  the MQTT service path. Allocation failure skips the state publish rather than
  publishing the previous module's data.

No task is moved to another core. Bus polling, HTTP observation intervals,
MQTT service/publish intervals, retained topics, payload fields, controls and
display traffic are unchanged. No live-state cache or change-only publish
filter has been added.

## Verification and Device Comparison

Native tests compare the production encoder against the old escaping rules for
every nonzero byte and 5,000 generated strings, including allocation/append
failure. A 4 KiB plain string uses one String concat instead of 4,096 per-byte
appends. Existing PSRAM-buffer numeric/escaping tests remain in place.

This operation-count reduction is not a measured ESP32 CPU improvement. After
updating only the master, compare with the same pages, HA polling interval,
MQTT settings and connected devices. Wait until startup/discovery has settled.
Record core 0/core 1 CPU, task CPU, MQTT build/publish timings and minimum free
heap/largest block. The reusable MQTT buffer can retain its high-water capacity;
verify steady-state memory as well as CPU. Hardware measurement is still needed.
