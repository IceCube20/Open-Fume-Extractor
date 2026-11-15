# Precise subsecond polling

Starting with 0.1.31RC, configured intervals below one second use monotonic,
start-to-start deadlines. For example, a 0.5-second interval and a 120 ms
response target starts at 0.5, 1.0, 1.5 seconds, not at 0.5, 1.12, 1.74 seconds.
The initial phase is relative to integration startup, not the wall clock.

The [HA coordinator scheduler](https://github.com/home-assistant/core/blob/2026.10.0/homeassistant/helpers/update_coordinator.py)
aligns its standard timer to a whole second plus a random phase. It explicitly
does not require exact timing. For our subsecond option, that can result in
past deadlines and uneven bursts. The same API and scheduling behavior were
checked in HA 2026.10.0 and 2026.11.0. Intervals from 1 to 60 seconds still use
that standard scheduler; defaults and existing options do not change.

Only the timer changes. The integration retains HA's refresh lock, auth/error
handling, availability, listeners and unload cancellation. It still reads one
shared cache-only master snapshot for all entities. It adds neither an endpoint
nor a second polling task. If a response runs beyond its next slot, the next
request is delayed by a full interval instead of catching up in a burst.

The configured interval is not a promise of end-to-end latency or lossless
events. Bus sampling, HTTP response time and HA's event loop still contribute.
A short output pulse can fall entirely between polls; MQTT or a future event
stream is preferable when every transition is required. A precise 0.5-second
setting can also increase master load versus an older irregular/slower cadence.

The change needs only an integration update and reload/HA restart, not a master
firmware update. It works with existing master HTTP APIs and preserves entity
IDs, aliases, profile metadata, inventory cleanup and dashboard appearance.

## Tests

Virtual-clock tests execute the production coordinator with small HA framework
stubs, not a complete HA runtime. They cover 0.5/0.75-second deadlines, response
latency, alternating real output specifications, no in-flight timer/overlap,
missed slots, rearming, standard-interval/retry delegation, polling disablement,
shutdown, no listeners, auth failure and transport recovery. Existing transport,
model, entity, inventory, frontend and install-package tests remain in place.
Device-side timing and a real HA runtime still need validation after install.
