# MQTT / Native Home Assistant Parity Audit

Date: 2026-10-10. Baseline: Master 2.0.36RC-Developer, native integration 0.1.0rc7.

## Correction Status

Master **2.0.40** and integration **0.1.0rc12** extend the input/output icon map
to all eight configurable Fan/IO and Fan/IO Pro channels. Previously only the
first two channels had direction-specific icons; higher channels fell back to
generic platform icons. Explicit descriptor icons remain authoritative. Eight
new DE/EN fixtures cover both module types, all channels, aliases and explicit
icon overrides, bringing the strict inventory/metadata comparison to 94 cases.

Master **2.0.39** and integration **0.1.0rc11** align integer output-feedback
percentages and explicit entity icons throughout the tested catalogues. JBC time
counters use days/hours/minutes in both transports, with numeric raw minutes in
the `counter_minutes` attribute. Non-time counters remain integers. Fan/IO Pro
exposes the filter-runtime reset button only when runtime monitoring is enabled;
both transports invoke the existing calibration action 7.

Module uptime moving backwards now invalidates the acknowledged Eco state in
all three master telemetry decoding paths. The existing scheduler reapplies it
without waiting for another power-saving transition. Visible UID labels in the
master web UI are now Serial number / Seriennummer; API identities are unchanged.

Verification for this revision: 144 Home Assistant tests, 93 firmware source
tests, native C++ command/catalogue/Eco tests, and desktop/mobile browser checks
passed. Counter tests render the actual MQTT discovery templates, including exact
hour/day boundaries and raw minute attributes. No hardware, live broker or Home
Assistant installation was changed by these checks.

Integration **0.1.0rc9** additionally aligns actual device-registry names/models,
integer CPU/module-count presentation, and old suggested precision settings.
Confirmed obsolete native entities are deleted, not just marked unavailable.
Cleanup requires two matching complete online inventories, excludes missing,
offline, updating and incomplete modules, and is scoped to the current OFE entry.
Current entity IDs and explicit user names/precision overrides are preserved.
Master **2.0.38** and integration **0.1.0rc10** extend the catalogue with supported
JBC station parameter controls, including global/individual level enables and
level selection/setpoints. Control support and raw command payloads are tested
against the actual firmware cache and protocol definitions. JTSE/CAP v1+ does
not receive level controls. Weller cached readings/filter totals remain available
on local disconnect, and its replacement interval is a select again. Developer
mode gates heap, minimum heap and maximum loop entities in both catalogues;
existing owned native diagnostic entries are removed even for offline modules.

The findings below document the **pre-fix baseline**, not the current build.
They were corrected in Master **2.0.37RC-Developer** and integration **0.1.0rc8**:

- MQTT now includes all nine previously missing master entities, JBC station
  name/port count, supported class-specific counters, hot-air selected flow,
  preheater selected thermocouple temperatures, and both Weller filter controls.
- Native Fan/IO RPM and Pro pressure duplication is removed; the surviving
  stable IDs are retained. MQTT faults are exposed with or without descriptors;
  legacy PWM/tachometer entities follow hardware capabilities.
- Per-field MQTT availability matches the native station/output/telemetry
  validity rules in the tested scenarios. Temporary offline modules retain
  discovery and expose an available connection sensor reporting offline.
- Universal/Modbus text commands no longer silently truncate to 30 bytes.
  MQTT and HTTP reject values exceeding the 188-byte transport limit. Target
  module validation still applies.
- The shared cache-only catalogue covers 73 JBC counter fields, including
  total/partial counters. Availability masks, labels and units are tested.
  Native 64-bit counters retain integer precision.
- MQTT manifests migrate the previous 72-entry layout, track counter keys and
  longer suffixes, and allow 192 entities per module. Counter values do not
  trigger discovery; supported-counter layout changes do.

The strict regression compares **86 DE/EN cases** with no missing entities or
metadata differences. A separate test renders the actual firmware availability
templates and compares them with native field validity. The original identity
migration caveat below remains: existing hardware-based native and address-based
MQTT unique IDs are intentionally not changed. This is not a live HA/broker or
physical-station certification. Native C++ tests also verify all 73 catalogue
offsets against the actual firmware cache structure, validity masks and exact
32/64-bit reads. The four-port station fixture stays within the manifest limit.

## Baseline Conclusion

The two integrations are **not fully equivalent**. Matching captions and icons
do not imply a matching inventory, availability policy, or command behavior.
No production firmware or integration code was changed during this audit.

## Findings

### P2: Master MQTT discovery omits nine native entities

The populated Master fixture produces 29 MQTT entities and 38 native entities.
MQTT lacks uptime, minimum free heap, maximum loop time, power-saving state,
WORK, output-feedback RPM, output-feedback power, output fault, and restart.
These are useful additions to MQTT, not native features to remove.

Sources: `../custom_components/open_fume_extractor/model.py:679` and
`../../OpenFumeExtractorMaster/src/MasterMqtt.inc.h:2256`.

### P2: Fan/IO native discovery duplicates existing descriptor sensors

With a descriptor and tachometer capability, native discovery creates the
descriptor RPM entity and an additional `rpm` entity. MQTT creates only the
descriptor entity. Native discovery also adds `fault`; MQTT omits that useful
fault sensor when a descriptor is present.

For Fan/IO Pro pressure modes 2 and 3, the actual descriptor uses entity 63,
key `pressure`. Native duplicate detection checks for key `filter_pressure`,
so it creates both `filter_pressure` and `entity_63_pressure`. Their units also
differ: the descriptor entity has `raw`, while the dedicated sensor has no unit.

Sources: `../custom_components/open_fume_extractor/model.py:591`,
`../custom_components/open_fume_extractor/model.py:615`,
`../../Module/FanIoProModule/FanIoProModule.ino:1276`, and
`../../OpenFumeExtractorMaster/src/MasterMqtt.inc.h:1531`.

The legacy MQTT fallback additionally creates power and RPM without separately
checking PWM and tachometer capability. A relay-only fixture therefore exposes
two MQTT entities that native discovery correctly omits. This is a fallback
case, not a claim that every current descriptor-based module exposes them.
Source: `../../OpenFumeExtractorMaster/src/MasterMqtt.inc.h:2201`.

### P2: JBC USB MQTT coverage is incomplete across station classes

All tested classes lack station name and port count in MQTT. Native discovery
also exposes supported raw counters, hot-air selected flow, and preheater
selected thermocouple temperatures that MQTT does not publish.

The counters in the table below are examples, not the complete station counter
inventory. Native counter captions currently expose raw field names, such as
`Port 1 cl counter work cycles`, rather than polished DE/EN labels.

Sources: `../custom_components/open_fume_extractor/model.py:455`,
`../custom_components/open_fume_extractor/model.py:538`, and
`../../OpenFumeExtractorMaster/src/MasterMqtt.inc.h:2096`.

### P2: Weller MQTT lacks two native controls

Native discovery offers a filter operating-time reset button and an adjustable
filter replacement interval. MQTT offers the interval as a sensor but does not
offer these two controls. Native control captions also remain English in the
German fixture because these control keys do not use the existing filter-sensor
caption mapping.

Sources: `../custom_components/open_fume_extractor/model.py:573` and
`../../OpenFumeExtractorMaster/src/MasterMqtt.inc.h:2234`.

### P2: Availability and lifecycle policies differ

Native JBC USB measurements require a valid, connected station; native Weller
measurements and most controls require recent local-UART data. MQTT module
entities use module-online availability, even when the attached station/device
is disconnected. Some MQTT measurements can consequently remain available with
cached values while native measurements become unavailable.

With an invalid JBC core, MQTT skips station-specific discovery entirely.
Native discovery can retain station entities with unavailable measurements.
This is separate from the already-tested FAE policy of keeping a cached Device
ID visible when the station is unplugged.

Sources: `../custom_components/open_fume_extractor/model.py:461`,
`../custom_components/open_fume_extractor/model.py:562`,
`../../OpenFumeExtractorMaster/src/MasterMqtt.inc.h:1777`, and
`../../OpenFumeExtractorMaster/src/MasterMqtt.inc.h:2098`.

### P2: Universal/Modbus text commands have different length behavior

MQTT silently truncates entity commands to 30 bytes. Native text controls allow
188 UTF-8 bytes, and the web handler constructs an entity payload up to
`MAX_PAYLOAD - 2` bytes. Long text commands can therefore differ by transport;
byte truncation can also split a UTF-8 character.

The final accepted length still depends on the target module/entity. This is
not a claim that every station accepts 188-byte text.

Sources: `../../OpenFumeExtractorMaster/src/MasterMqtt.inc.h:2811`,
`../custom_components/open_fume_extractor/text.py:13`, and
`../../OpenFumeExtractorMaster/src/WebControls.inc.h:740`.

## Identity Risk Requiring A Migration Policy

Native entity identity includes hardware UID, profile entity ID, and profile
key. MQTT identity uses module address and profile entity ID. Readdressing or
replacing a profile key therefore behaves differently. In particular, reusing
the same numeric profile ID at the same address can leave an MQTT automation
attached to a different channel, whereas native discovery creates a new identity.

Do not blindly change existing unique IDs: that would break users' dashboards
and automations. Define an explicit migration policy and test replacement,
readdressing, class changes, and profile-slot reuse first.

Sources: `../custom_components/open_fume_extractor/model.py:669`,
`../custom_components/open_fume_extractor/entity.py:23`, and
`../../OpenFumeExtractorMaster/src/MasterMqtt.inc.h:1747`.

## Discovery Fixture Results

Counts include common module entities. JBC fixtures use **one port** and only
the counters listed in the fixture, not every possible counter. The rows below
have identical results in German and English.

| Fixture | MQTT | Native | Inventory difference |
| --- | ---: | ---: | --- |
| Master | 29 | 38 | Nine native-only entities |
| JBC FAE | 16 | 16 | None |
| JBC USB DDE | 26 | 29 | Station name, ports, one work counter |
| JBC USB JTSE | 26 | 31 | Station name, ports, selected flow, two counters |
| JBC USB CLMU | 23 | 27 | Station name, ports, two counters |
| JBC USB PHBE | 28 | 34 | Station name, ports, four selected TC temperatures |
| JBC USB F2 | 24 | 26 | Station name and ports |
| JBC USB SF | 25 | 27 | Station name and ports |
| JBC USB unknown class | 20 | 22 | Station name and ports |
| JBC USB disconnected, cached DDE core | 26 | 28 | Station name and ports; availability differs |
| JBC USB invalid, cached DDE core | 11 | 28 | Native retains station entities |
| Fan/IO legacy, PWM + relay + tach | 19 | 19 | None |
| Fan/IO legacy, relay only | 19 | 17 | MQTT-only power and RPM |
| Fan/IO descriptor | 16 | 18 | Native duplicate RPM and native-only fault |
| Fan/IO Pro filter off | 16 | 18 | Native duplicate RPM and native-only fault |
| Fan/IO Pro runtime only | 20 | 22 | Native duplicate RPM and native-only fault |
| Fan/IO Pro pressure only | 19 | 22 | Also native duplicate pressure |
| Fan/IO Pro runtime + pressure | 22 | 25 | Also native duplicate pressure |
| Weller connected | 20 | 22 | Two native-only controls |
| Weller disconnected | 20 | 22 | Two controls; availability differs |
| Display 320x480 | 15 | 15 | None |
| Display 800x480 | 15 | 15 | None |
| Display ST7796 320x480 | 15 | 15 | None |
| Universal RS232, 32 switch entities | 43 | 43 | None |
| Modbus RTU, 32 switch entities | 43 | 43 | None |
| Universal RS232, mixed profile | 19 | 19 | None in discovery metadata |
| Modbus RTU, mixed profile | 19 | 19 | None in discovery metadata |

No differences were found in captions, platforms, icons, units, device classes,
state classes, diagnostic categories, number bounds, or select options for the
**shared entities in these fixtures**. This does not mean missing entities match.

Mapped numeric sensor values were initially suspected. Current Universal and
Modbus builders convert terminal mapping/DHM results to descriptor type `text`.
That supported path preserves the processed text in native discovery. A numeric
descriptor containing text would behave differently, but it is not established
as a bug in the current builders.

## Verification And Limits

- 54 discovery fixtures: 27 scenarios in DE and EN.
- 114 existing Home Assistant tests passed.
- 93 existing firmware source tests passed.
- The audit executes translated firmware discovery branches with captured
  publish calls and compares them with actual native `build_specs` results.
- Descriptor definitions and JBC core projections are fixtures. Descriptor
  parsing, station-wire decoding, live MQTT retention, Home Assistant registry
  migration, and actual device commands are not emulated by this audit.
- Matching display results cover their shared HA discovery path, not a new
  performance/hardware test of the three panel drivers or WLAN transport.
- No live Home Assistant instance or broker was changed. No firmware was built,
  signed, uploaded, or flashed in this audit.

Run from the firmware-project root:

```powershell
python HomeAssistant/tools/audit_parity.py
```

The tool prints observations, not a passing all-parity assertion. Existing tests
passing is therefore not evidence that the two transports are fully equivalent.

## Recommended Implementation Order

1. Remove native duplicate Fan/IO sensors and add missing MQTT fault coverage.
2. Add missing Master, JBC USB, and Weller MQTT entities and controls.
3. Agree on per-field availability, retained identity, and write-only state rules.
4. Align command validation/length handling without silently truncating text.
5. Convert this audit into strict parity regression tests for supported behavior,
   including disconnects, profile replacement, and disabled hardware functions.

Keep existing unique IDs unless a deliberate migration is implemented. Preserve
useful native functionality and MQTT compatibility rather than deleting entities
merely to make the counts equal.
