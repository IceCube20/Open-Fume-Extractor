# Addressed Home Metrics Tests

Run `./run.ps1`. The harness compiles the actual Master snapshot builder, shared
wire codec/cache and current display live-status parser with MSVC. Build products
go to the system temporary directory. It does not flash a device.

Checks cover independent addresses and types, safe eviction and 49-day clock
wrap, 32-bit filter times, disabled runtime/pressure features, cached disconnected
Weller readings, already-processed profile values, truncated/invalid wire data,
mixed native/entity/operational records and refreshing unchanged stale values.

The existing `jbc_detail_ui` harness renders the actual Home builders and shared
card/header code at 480x320 and 800x480 in both themes, including two modules of
each non-JBC family. It checks exact per-address values, direct detail navigation,
long aliases, hidden features, module removal, uniform accent stripes, fixed large
screen controls, animation lifecycle and alarm-free/warning/critical header icons.
Source guards also verify opt-in negotiation for older firmware and identical
headers across display variants. ESP32 compiler checks remain separate.
