# Developer I/O Profiling: 2.0.93 RC

This build extends the 2.0.92 JSON optimization with measurement only. It does
not move tasks, reduce polling, cache live state or change MQTT topics/retention,
retry behavior or transport blocks. Only the master needs updating.

## New Measurements

The status page shows **HTTP / JSON** in developer mode, split by:

- Status requests (`/state`).
- Native Home Assistant requests (`/state?ha=1`).
- Bus diagnostics (`/diagnostics/state`).

Each line shows requests per second, then JSON build and HTTP send timings,
each as average / maximum milliseconds and elapsed milliseconds per second.
JSON throughput counts successfully built body bytes, not wire throughput.

MQTT retains its aggregate live-data preparation, Publish and Client loop
measurements. Master/module preparation is additionally shown separately.
Socket write time observes the existing plain/TLS clients through their virtual
write interface. It records accepted bytes and short writes without retrying or
changing the result returned to PubSubClient.

Socket times overlap Publish and Client loop activity and can also include
connection-control writes. Do not add these numbers to obtain total CPU load.
All timings include waiting and preemption; successful socket writes do not
confirm delivery to the broker/browser. Use the existing task CPU/core CPU
measurements for actual processor utilization.

Counters use bounded, allocation-free five-second windows. With developer mode
off, no new timing calls are made in the transport/phase scopes and no new
performance fields are included in normal status JSON. Window reset still runs
with the existing service tick so old samples do not survive a mode change.

## Comparing on Hardware

1. Enable developer mode and wait at least ten seconds after startup/discovery.
2. Keep the same HA polling settings, MQTT configuration and connected modules.
3. Capture CPU/core CPU, Master Runtime, MQTT and HTTP / JSON from the status page.
4. Repeat with the bus diagnostics tab closed, then with it open. Its build/send
   measurements should appear only while diagnostics requests are being made.
5. Compare steady windows, not just one maximum or one CPU sample.

Native tests cover actual production hooks, master/module aggregation, window
and timer wrap, disabled timing, mode changes, HTTP without MQTT, exact transport
delegation, short writes and unchanged transmitted bytes. Browser tests check
developer-only visibility and layout at desktop/tablet/320px/390px widths.
