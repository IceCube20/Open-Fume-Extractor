# MQTT Performance Diagnostics

Master firmware 2.0.42 adds an MQTT block to **Status > Master-System** in developer mode. MQTT remains optional; publish intervals, topics, payloads and Home Assistant entity updates are unchanged.

The block displays the latest completed five-second sample window:

- **Live data preparation:** time spent constructing master and module state payloads. Discovery metadata construction is not included.
- **Publish:** time inside every MQTT publish call, including discovery and availability messages.
- **Client loop:** time inside the MQTT client loop, including received command callbacks.
- **msg/s:** attempted publishes per second. **KiB/s:** successfully handed-off payload bytes, excluding topic names and transport overhead.
- **Failed:** publish calls returning false during the sample window. A successful return is not a broker acknowledgement.

Each phase shows average / maximum elapsed milliseconds per call, followed by elapsed milliseconds per second. These are wall-clock measurements, not task CPU utilization. Preemption, TLS and socket waits can increase them. Command callbacks can themselves publish, so client-loop and publish time may overlap; do not add the phases as independent CPU percentages.

Before the first completed window, the UI shows the current partial sample. Disabling MQTT clears the display. Collection is enabled only in developer mode and uses fixed-size counters without allocations or extra network requests.

For a useful comparison, keep modules, display pages and broker connection unchanged. Allow startup discovery to finish, then record CPU, loop maxima and this block after 30-60 seconds. Compare several windows with MQTT enabled and disabled. Brief peaks alone do not establish a CPU or memory regression.

## Availability Optimization (2.0.43)

Module `status` topics now publish their retained `online` / `offline` value only when it changes or has not yet been sent successfully in the current connection. A failed publish is retried on the next normal module cycle. Every successful MQTT reconnect resets the cache, including after broker, credentials or base-topic changes. Removing a retained module status also invalidates its address in the current namespace.

The master status, Last Will, explicit shutdown/offline announcements, discovery, entity identities and command handling are unchanged. Master/module live state payloads, including offline snapshots, keep their previous update intervals. New subscribers receive the retained availability without waiting for a new change. The cache uses 64 static bytes and no dynamic allocations.

With seven online modules and the default 350 ms state interval, steady-state publishes should fall from roughly 43 to 23 per second. Actual rates vary with service scheduling and other messages. Compare the Publish phase and measured CPU after flashing; this estimate is not a promise of a proportional CPU reduction.
