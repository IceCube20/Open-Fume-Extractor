# Master Runtime Diagnostics

Master **2.0.73RC** adds a **Master Runtime** block to the Status page and Bus
Diagnostics while developer mode is enabled. It reuses the existing page polling;
it does not create a diagnostic task or additional network requests.

## Task CPU Load

Four existing application tasks are sampled approximately once per second:

- **Control loop:** Arduino loop, including bus scheduling, queued controls,
  module OTA, logic, local CLI/LED handling and housekeeping.
- **HTTP / WLAN:** HTTP requests, network service and captive DNS when applicable.
- **MQTT:** broker service, discovery, state preparation and publishing.
- **Heap diagnostics:** developer-mode heap probing.

Percentages come from differences in each task's FreeRTOS runtime counter divided
by the elapsed runtime-clock interval. **100% means one fully occupied core**;
these percentages use a different denominator from the dual-core total CPU load.
The existing overall and per-core CPU values are unchanged.

The usual configuration pins these four tasks to core 1. The loop's configured
core is reported separately; a floating/unknown core is not attributed to core 1.
ESP-IDF networking, Bluetooth, interrupt service and other system work are not
enumerated. Do not expect these four percentages to account for every CPU cycle.

The first sample after enabling developer mode establishes a baseline and shows
a dash. Missing runtime statistics or an absent task also show a dash rather than
0%. If HTTP task creation fell back to inline servicing, that work belongs to the
control-loop figure. An allocated but idle MQTT task can legitimately read 0%.

## Loop Phase Timing

The same block shows average/maximum elapsed milliseconds per call and elapsed
milliseconds per second for local service, control commands, module OTA, bus
scheduler, logic and maintenance. These are **wall-clock timings, not CPU load**.
They include bus/socket waits and time during which another task preempts the
loop. They must not be added to the task CPU percentages. Scheduled idle delay is
not included in these phases. Existing loop maxima retain their previous scope.

Counters use fixed-size storage, publish a coherent snapshot under a short lock,
and support runtime-clock and `millis()` wraparound. Runtime sampling does not
scan task stacks or enumerate all system tasks. Disabling developer mode clears
the phase window and task baselines on the next loop iteration and omits the
profile from both JSON endpoints. Nothing is persisted to NVS.

## Comparing Results

After startup/OTA settles, keep modules and display pages unchanged. Record several
successive windows of overall/per-core CPU and Master Runtime. If HTTP or MQTT
dominates core 1, investigate that service before changing the bus scheduler. If
the control loop dominates, use phase totals and the existing scheduler slow-job
name to narrow it down. High elapsed scheduler time alone does not prove high
scheduler CPU usage. Task placement, priorities, polling and control behavior are
unchanged by this diagnostic revision.

## Direct JSON Serialization (2.0.74RC)

The diagnostics state response now uses the same capability-aware text buffer as
the main state response. Its large allocation is reused only in PSRAM. Without
PSRAM, a temporary internal buffer is released after each response. An allocation
failure returns HTTP 503 instead of sending incomplete JSON.

Integer encoding retains the original integer width and avoids general-purpose
64-bit `printf` formatting for every small value. Selected frequently repeated
text fields are escaped directly into the response buffer, preserving the prior
escaping behavior and UTF-8 bytes without a temporary Arduino String. Native
tests compare all integer widths/limits and random byte strings against the
previous formatting behavior.

Responses are still rebuilt from current state on every request. There is no
response TTL or new cache invalidation policy. Browser observation intervals,
Home Assistant API fields, bus polling, MQTT intervals and task placement remain
unchanged. A CPU reduction must be verified on the device with the same clients
and workload; host formatting tests are not an ESP32 performance measurement.

The WiFi timing block and controls are hidden outside developer mode, including
after switching modes on an already open diagnostics page. Timing text is inset
14 pixels on each side of the module card and wraps within the available width.
