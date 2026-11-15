# HA metadata preparation, 2.0.94RC

The 2.0.93 sample measured native HA JSON preparation at about 129 ms per
request, versus 44 ms for the normal status response. These are elapsed times,
not per-core CPU percentages.

## Change

- Reuse descriptor-derived entity definitions and native HA routing lists.
- Store optional keys and JSON fragments in PSRAM only, owned by the HTTP task.
- Compare exact, length-prefixed metadata fields, not CRCs, hashes or a TTL.
- Invalidate immediately for descriptor contents/validity, language, module
  identity/address/type/capabilities, aliases, online state and route selections.
  Registry order/count are included because they affect automatic routing.
- On allocation failure or configuration changes during a rebuild, fall back
  to the existing fresh builder. Failed/partial fragments are never reusable.

Live states, I/O, alarms, runtime counters, CPU, JBC control values and
availability remain fresh. No polling interval, bus schedule, MQTT cadence,
HA response field or frontend appearance changes. JBC control descriptors are
not cached in this step because their availability and bounds depend on station
capabilities; their existing projection is unchanged.

## Verification

Native tests compile the actual descriptor builder, cache wrapper and routing
key from production source, alongside the real descriptor parser. Tests cover
byte-for-byte fresh/cached descriptor output, repeated allocation-free hits,
language/profile edits with unchanged CRC, module replacement/reordering,
removal, aliases, selections, validity/online transitions, PSRAM allocation
failure and changes during a rebuild. Existing Python regressions and the
ESP32 syntax check are also run. No device performance gain is claimed yet.

After flashing only the master, compare the 2.0.93/2.0.94 HA preparation and
per-core/task CPU measurements under identical connected-module, MQTT and
browser conditions, after caches warm up. No HA reinstall is required.
