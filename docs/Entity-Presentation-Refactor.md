# Shared Entity Presentation

The master uses `src/EntityPresentation.h` and `src/EntityPresentation.cpp` to
prepare descriptor-based entities for the web UI, MQTT and display detail pages.
This applies to Universal RS232, Modbus RTU and descriptor-based Fan/IO channels.
Native module-specific telemetry and controls retain their existing transport.

## Ownership

The core is independent of Arduino, NVS, Wi-Fi, RS485 and the scheduler. It uses
bounded views and caller-owned buffers, with no heap allocation, locks or I/O.
Views borrow descriptor memory; callers must not retain them across descriptor
replacement. The existing master-loop ownership of module data is unchanged.

## Value Contract

- Module profile builders remain authoritative for scale, offset, bit masks,
  value maps and formatted time values. The master does not apply these again.
- Raw values are decoded once using the same UTF-8/text and little-endian binary
  rules. Numeric profile entities use signed int32 values, as published by the
  builders; built-in counters retain unsigned decoding. Diagnostic hexadecimal
  bytes remain available unchanged.
- Select labels, indices and command values use one resolver. Exact matches take
  precedence over legacy numeric-prefix/scaled matching. Formatted d/h/m values
  can be matched against the corresponding configured duration option.
- Boolean values use the same interpretation in routing, MQTT, web controls and
  display details, including textual `off` and `false`.
- Numeric duration fallback is only enabled by explicit `time_display=dhm` (or
  `tf=dhm`) and a supported time base. Already formatted text passes through.
- DE/EN labels prefer the requested language, then the other language, then the
  entity key. Custom label underscores are not rewritten in MQTT alone.
- Existing display packet sizes and versions stay unchanged. Fixed-size display
  strings are truncated at a UTF-8 character boundary.

## Web API

The existing `universal_entities[].ascii` field contains the common decoded raw
value. `hex`, `len`, `age_ms` and the legacy numeric fields remain available.
`display_text` is emitted only when the presentation differs from the raw text;
`selected_value` contains the configured command value for a resolved select.
The browser keeps existing descriptor caching and preserves focused/dirty inputs
during live updates. Descriptor metadata stays out of the frequent `/state`
response unless explicitly requested.

## Verification

Run `tools/tests/entity_presentation_native/run.ps1`, then
`node tools/tests/test_entity_presentation.js` from the project directory.
These tests compile the actual core and exercise the actual web render/update
functions with native-generated fixtures. Existing routing, logic, typography,
diagnostics and update regressions remain applicable.

This refactor does not change bus timing, OTA routing, pin configuration, NVS
layout or display rendering drivers. Continuous operation remains transient.
Host benchmarks do not establish the CPU improvement on an ESP32; verify device
CPU load, heap and module response latency on the running test system.
