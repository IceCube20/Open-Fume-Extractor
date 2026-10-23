# Open Fume Extractor for Home Assistant

An initial local, broker-free custom integration. Master firmware **2.0.31 or
later** is required; use **2.0.53+** with **0.1.0rc24** for the aligned MQTT/native
catalogue, station parameter controls and developer-only diagnostics. No module firmware changes are required.
This version is ready for a Home Assistant test installation, not a claim of
certification or full feature parity with the existing MQTT discovery.

## Installation

1. Update the master and complete its Wi-Fi and web-password setup.
2. Place `custom_components/open_fume_extractor` in the Home Assistant
   configuration directory under `custom_components`.
3. Restart Home Assistant.
4. Open **Settings > Devices & services > Add integration > Open Fume Extractor**.
5. Enter the master's IP/hostname, web username and your current web password.

Use **Reconfigure** on the integration's menu to change the host or web login.
Changed credentials also trigger a reauthentication flow rather than requiring
you to delete the integration. Entity IDs remain tied to hardware identities.

mDNS discovery is available for the default `open-fume-extractor-*` hostname.
Custom master hostnames can be added manually. The master and modules use hardware
identities; changing an alias or bus address does not create new entities.

## Included entities and controls

- Master power, output feedback, activity, alarms, afterrun and power-saving status.
- Suction level, custom power, work/stand afterrun, stand intakes and Continuous.
- Main input/output selection and global status-LED enable/brightness controls.
- One master device and child devices for modules, with connection, firmware,
  CPU, uptime and transport diagnostics. Heap (KiB), minimum heap and maximum
  loop time are exposed only while the master's developer mode is enabled.
- Display brightness, language, theme, screensaver and master/module restart.
- JBC FAE station connection, work/stand and identity.
- JBC USB station identity/error, connection mode and class-specific port values:
  soldering temperatures/power/sleep countdown, hot-air flow/stop countdown,
  cleaner motors/door/counters, preheater thermocouples/heater/power,
  extractor intakes and feeder speed/length/program. Unsupported values disappear
  from the registry after two matching, complete online snapshots confirm a
  station-class change. Explicit station absence, reported twice by an online
  JBC USB module, also removes station-specific entities. An offline module or
  failed poll does not authorize deletion.
- JBC USB supported station settings: port temperature and hot-air flow, global
  and individual temperature-level enable switches, selected level and level
  setpoints, preheater thermocouple setpoints and extractor flow. Read-back
  confirmation uses the same queued write transaction as the web UI; a request
  does not optimistically replace the displayed station value. Unsupported
  classes/settings are omitted, including temperature levels on JTSE/CAP v1+.
- Weller fan/light/power, RPM and filter condition, operating time, replacement
  interval and remaining time; interval dropdown and filter reset. Cached readings
  and filter totals remain visible when the local Weller is disconnected but its
  OFE module is online. Live controls and filter reset still require the Weller.
- Fan/IO main output controls where not already descriptor-backed, RPM/faults,
  and Fan/IO Pro derived filter lifetime/remaining time and filter-runtime reset
  when runtime tracking is on.
- Descriptor-backed Fan/IO, Fan/IO Pro, Universal RS232 and Modbus profile entities:
  sensors, inputs, switches, numbers, selects, text controls and buttons, respecting RO/RW/WO.
- Values use the master's builder-postprocessed text and selection mapping;
  numeric scales are not applied a second time.
- Dynamic profile/channel changes add entities without restarting Home Assistant.
  Confirmed removed entities are deleted from the native entity registry.
  Profile entities include their machine key in the identity: reusing a profile
  slot for a different key does not silently redirect an existing automation.

Hardware/profile editing and firmware OTA remain in the web UI. The automated MQTT/native catalogue comparison covers
the supported master/module cases; it is not a full web-control parity claim.

## Updating from earlier versions

RC21 removes JBC USB station-specific entities after two matching successful
polls explicitly report no station connected. Module diagnostics, station
connection, Work and Stand remain; supported station entities return on reconnect.
This also ignores stale cached ports and controls in a disconnected report.
MQTT uses the same policy with Master 2.0.53, confirming two discovery snapshots
(normally about 15-30 seconds, followed by incremental retained-topic cleanup).
No JBC USB module update is required.

For MQTT discovery and cleanup, enable **Home Assistant Discovery** under the
master's **Network/System > MQTT** settings and save. The normal discovery
prefix is `homeassistant`. MQTT live state publishing continues even with
discovery disabled, but new devices and retained entity removals do not.

RC20 repairs stale devices and entities. Modules removed from the master's
inventory are removed from this integration after two complete matching lists;
modules still listed as offline are preserved. Cleanup also covers registry
entries left from before a Home Assistant restart. It is paused during the
master's first 30 seconds, firmware updates, failed polls and incomplete lists.
The master and devices owned/shared by other integrations are not deleted.

JBC station changes remove unsupported entities after two matching connected
snapshots. The authoritative master counter projection no longer falls back to
old raw counter caches when the current station exposes no counters; counter
families and port counts are checked before creating entities. This prevents
obsolete counters being created again immediately after cleanup.

The native cleanup works with the existing Master 2.0.51 firmware. The separate
MQTT retained-discovery repair requires Master 2.0.52; no module flash is needed.

RC19 adds **Off** to the Weller filter replacement interval selector.
Use **Master 2.0.51+** and **Weller module 1.1.83+**: selecting Off queues
`f000` followed by `g000`, each with the Weller checksum, as in the original
ESPHome configuration. This disables the programmed interval and resets its
operating-time counter. Select a positive interval to enable it again.
The independently reported filter condition is not changed or hidden.
The observed `r001`/`r000` commands have an unconfirmed purpose and are not
automatically sent during connection.

Replace the component folder with **0.1.0rc24** and restart Home Assistant. Do not
delete and recreate the integration: existing hardware-based entity IDs remain
unchanged. This update fixes unavailable entities caused by a collision with HA's
reserved `platform` attribute and removes the deprecated device-link parameter.
The original project logo is included in `brand/`; local logos require Home
Assistant **2026.3 or later**. Reload the browser after restarting if the icon
is still cached.

RC16 with **Master 2.0.41+** removes repeated addresses from module entity captions.
Address and alias remain on the device/card header; entity IDs and user overrides
are preserved. Cards have one header, larger OFE/EVT indicators and the master's
module icons. MQTT state publishing reuses descriptor views instead of repeatedly
allocating discovery metadata; the publishing interval is unchanged. No module
firmware update is required.

Device pages and OFE cards show short entity captions such as `Power`. Home
Assistant may still prepend the device name in global entity lists; this is its
standard device-based naming behavior, shared by MQTT and the native integration.

RC13 adds device summaries, connection/transport/Eco indicators and a power bar
to the included dashboard cards. Click a summary value for HA's more-info dialog.
Controls remain native HA rows, and value updates preserve their focus. This is
an integration-only update; it does not require another firmware update.

RC12 with **Master 2.0.40+** uses matching input/output icons for all eight
configurable Fan/IO and Fan/IO Pro channels, not just IN1/IN2 and OUT1/OUT2.
Explicit descriptor icons and user icon overrides remain unchanged.

RC11 requires **Master 2.0.39+** for the Fan/IO Pro filter-runtime reset button,
complete default icon catalogue, whole-percent output feedback, and automatic Eco
resynchronization after a fast module reboot. No module firmware update is needed.
JBC USB time counters display days/hours/minutes; their raw minute count remains in
the `counter_minutes` state attribute in MQTT and the native integration. Non-time
counters remain numeric. Existing statistics for the old numeric time sensors are
not converted; automations relying on their numeric state should use this attribute.
User-defined icons and Home Assistant display-precision overrides are preserved.

RC10 requires **Master 2.0.38+** for JBC parameter metadata and writes. It restores
the Weller replacement-interval dropdown (1-23 hours, 1-6 days, or 6 days 16 hours)
and retains cached Weller readings on local disconnect. Switching developer mode
off removes the owned heap/maximum-loop entities in both integrations; switching
it back on recreates them. MQTT discovery refreshes automatically after the
master update. Existing obsolete number entities for the Weller interval are
replaced by select entities, so dashboard references to that old number may need
updating. CPU and module count use whole-number display precision.

RC3 aligns built-in icons and device classes with MQTT, adds sensor display
precision (RC6 also rounds the heap state to whole KiB, matching MQTT),
and uses number boxes for afterrun seconds instead of 0-3600 sliders. JBC tools
and errors now use the same friendly mappings as the master/MQTT. Station class,
cleaner/preheater/feeder port state and descriptorless Fan/IO Pro pressure and
filter mode are included. Profile roles/icons are respected; a pressure entity
already supplied by a descriptor is not duplicated.

RC4 introduced module entity captions such as `0x20 Ventil`, with the module alias
only on the device/card header. JBC FAE and USB Stand both use the parking icon.
Master and module uptime display as text like `1d 2h 3m`, using the same rounding
as MQTT. Raw uptime remains available in the `uptime_seconds` attribute; numeric
automations should read that attribute instead of the now-formatted state.
Uptime no longer has a numeric unit or statistics state class.

Existing entity IDs are retained. MQTT and the direct integration still have
separate entity registries. Update the master to **2.0.33+** for the corresponding
MQTT captions and FAE Stand icon; no module firmware update is required. The
direct integration also works with the existing **2.0.31+** HA API. User-defined
HA name/icon overrides are not overwritten; clear those overrides on individual
entities if you want the new defaults.

RC5 keeps JBC Work/Stand available while the OFE module is online, even when the
station is unplugged: both show off rather than unavailable. Station-specific
details still become unavailable on disconnect. FAE Stand is derived from a
connected station handshake and inactive Work, matching the master web UI; USB
Stand uses the actual station mask. An offline module remains unavailable.

Master and module hardware UIDs are included as diagnostic serial-number sensors
as well as device information. **Master 2.0.34+** also shows the master serial
number in the web status overview and supplies serial numbers through MQTT
device discovery and state/sensor topics. These are OFE controller identities,
not the separate JBC station device ID. Existing MQTT/HA identities are unchanged.

RC6 compares built-in names, platforms, icons, units and diagnostic categories
against the firmware's MQTT discovery definitions. Built-in names follow the
master's German/English UI language; user-defined aliases remain unchanged.
JBC USB values use the same cache-only core projection as MQTT, including
class-specific ports, power/flow scaling and countdown formatting. Afterrun and
filter times share MQTT's text formatting; `afterrun_seconds` retains the raw
remaining time for numeric automations. Writable profile text is a text control,
not a read-only sensor. Filter functions disabled in Fan/IO Pro are omitted.

Update the master to **2.0.35+** for the corresponding MQTT metadata and native
routing/LED controls. Older supported masters keep the legacy readout fallback.
MQTT is still independent and can remain enabled. These are shared defaults,
not a promise that user overrides or historical registry entries will match.

RC7 fixes the concrete FAE MQTT/native comparison: module CPU, firmware, heap,
uptime, connection, minimum heap, maximum loop time, transport and restart belong
to Diagnostics on both paths. Power saving is an operational binary sensor with
the sprout icon. MQTT now exposes the five previously native-only common module
entities (minimum heap, loop time, transport, power saving and restart).
FAE station text is `-` when disconnected; the cached station device ID remains
visible while the OFE module is online. This overrides RC5's blanket rule for
these two FAE identity fields; an offline OFE module is still unavailable.
German diagnostic captions and profile icons are also supplied correctly.
The FAE regression test executes the firmware's discovery publisher and compares
the full module inventory, metadata and connection transitions with native specs.
Other station-class-specific extras described above are not yet guaranteed to
have identical inventories. No user name/icon overrides are overwritten.

Use **Master 2.0.37+** with RC9 for the aligned MQTT/native entities and descriptor icons.

RC8 adds the missing MQTT master diagnostics, output feedback and restart,
Weller filter reset/interval controls, and class-specific JBC counters and selected
flow/thermocouple temperatures. Both paths use the same cached JBC projection;
there are no additional device-bus polls. Disconnected/stale readings are marked
unavailable, while the module connection sensor remains usable to report offline.
Temporary disconnections keep the MQTT inventory; removing a module or changing
station capabilities reconciles retained discovery through the manifest.

The native descriptor RPM and dedicated filter-pressure sensor replace duplicate
Fan/IO readings. Their surviving unique IDs are unchanged. Previously created
duplicate native sensors are removed automatically once their module reports a
confirmed, complete inventory. Update dashboards/automations referencing these
obsolete duplicates to use the surviving entity. Native IDs remain hardware-based;
existing MQTT IDs remain address-based.
These naming schemes are deliberately not rekeyed by this update.
Flash only the master, update the HA component, and restart Home Assistant.
The master republishes MQTT discovery after connecting; no MQTT entity deletion
or module firmware update is required. Disabled profile time formatting (`none`)
keeps the native value numeric with its original unit, matching MQTT.

RC9 aligns device names/models (including `0x01 Open Fume Extractor Master`) and
updates existing device metadata without overwriting user device names. CPU and
module counts use integer states and zero suggested decimals. Old suggested
display precision is updated; explicit HA user precision settings take priority.
Registry cleanup covers removed channels, component changes, obsolete duplicates
and unsupported JBC station entities, including disabled entries. Offline, missing,
updating or incomplete modules are never deletion authorities. Reappearing channels
can be added again using their original unique IDs; current entities keep their IDs.
This is an integration-only update: no further master/module flash is needed if
Master 2.0.37 is already installed.

## Alarm lists

With **Master 2.0.47+** and **integration 0.1.0rc18**, both the **Alarm Count** and
**Alarm Text** sensors expose `alarms` as a JSON list of complete messages and `alarm_count` as an
attribute on both MQTT and the direct integration. Each message includes its
module or source. An empty list means there are no active alarms. The **Alarm Count**
state remains the numeric count. **Alarm Text** is a separate, visible sensor on
the master device in both integrations, using the same bell icon. It shows `OK`
with no active alarms or messages separated by semicolons. Its state is limited
to 255 characters to fit HA's state limit; the `alarms` attribute retains every
complete message. RC18 restores this sensor after its removal in RC17, reusing
its previous identity. The web alarm display remains unchanged.

The OFE Master and Overview cards display active alarms as a separate list and
hide it when the list is empty or the master is unavailable. The additional
**OFE Alarms** card works with **MQTT or native** sensors: select your Alarm Count
sensor in its graphical editor. It displays each message separately, shows
"No active alarms" for an empty list and "Unavailable" for an offline master.
It consumes HA states without extra network polling. Use the same JavaScript
resource as the other OFE cards. Example:

```yaml
type: custom:ofe-alarms-card
entity: sensor.your_master_alarm_count
```

For an ordinary HA dashboard, a Markdown card can read the same attribute:

```yaml
type: markdown
title: Active alarms
content: |-
  {% for alarm in state_attr('sensor.your_master_alarm_count', 'alarms') or [] %}
  - {{ alarm }}
  {% else %}
  No active alarms.
  {% endfor %}
```

Replace the example entity ID with your actual **Alarm Count** sensor.

## Dashboard cards

The integration includes **OFE Master**, **OFE Module**, **OFE Overview** and
**OFE Alarms** cards. The Alarms card also supports MQTT entities.
Each device has a header, connection/transport/Eco indicators and up to four
device-specific summary values. Master cards show requested power, feedback RPM,
afterrun and alarms; module summaries adapt to fan/filter, JBC station, display
or profile values. Click a value to open its HA more-info dialog. A power bar
is shown directly below the power value when a valid percentage is available.
Compact device headers and separated summary values follow HA's light/dark theme.
OFE and EVT indicators mirror the master's LED event colors/effects, including
the white/purple OFE power-saving effect. Two read-only LED sensors per device
also expose the event as text, with color/effect metadata for dashboards and
automations. These sensors do not require developer mode. Missing/invalid LED
telemetry and offline devices are shown without animation; global LED disable
turns the indicators off. Click an indicator to open the corresponding sensor.
Animations are browser-side, respect reduced-motion settings, and do not add
HTTP polling. Their phase is not synchronized with the physical LEDs; short
activity pulses between HA polls cannot be captured. Missing values are not invented.
They use native Home Assistant entity rows for controls, with no MQTT, Mushroom
or other card dependency. Overview groups the master and every module; Module
groups controls, filters and JBC ports. Cards follow hardware identities and
aliases rather than hard-coded entity IDs. Offline devices remain visible;
removed or no-longer-supported entities are excluded. Diagnostics are optional.

From **0.1.0rc25**, UI/storage-mode Lovelace resources are registered and
version-updated automatically when the integration starts. Existing OFE entries
are migrated and duplicate entries for this exact local bundle are removed.
After updating, restart HA and fully reload the browser/app. Then edit your
dashboard, add a card and choose **OFE Overview**, **OFE Master** or **OFE Module**.
The graphical editor selects master/module and controls diagnostics and summaries.

YAML-managed resources remain user-owned. Add the unversioned
`/open_fume_extractor/ofe-cards.js` once with `type: module`, then fully reload
the browser after updates. No automatic YAML edits or dashboard rewrites occur.
If automatic storage registration fails, the manual fallback is
`/open_fume_extractor/ofe-cards.js?v=0.1.0rc25` as a **JavaScript module** under
**Settings > Dashboards > Resources**. Update an existing entry, do not add a
second copy. Entity setup continues even if resource registration fails.

For a manual card, an overview needs only:

```yaml
type: custom:ofe-overview-card
show_diagnostics: false
show_summary: true
```

With multiple masters, select a master in the editor or specify `master_uid`.
For a module card, specify its hardware `module_uid` (the editor lists its alias):

```yaml
type: custom:ofe-module-card
master_uid: "0000000012345678"
module_uid: "2000000012345678"
show_diagnostics: false
show_summary: true
```

Use your actual UIDs, not these example values. Optional `title` changes a card's
heading. German/English section titles follow the HA frontend language; native
control rendering follows HA. Built-in entity labels follow the master language;
module/profile aliases are retained. Your existing dashboards
are not automatically overwritten. Only this integration's local card-resource
registration is managed automatically in storage mode.

For existing **MQTT** heap discovery warnings, separately rebuild/flash master
**2.0.32**. Master and module heap discovery now uses `KiB`, matching its 1024-byte
conversion. The same retained discovery topics/unique IDs are republished when
MQTT reconnects; MQTT itself is not removed or disabled by this integration.

## Polling and availability

A shared status coordinator polls cached master state every **5 seconds** by
default (configurable from 1 to 60 seconds in the integration's options).
Saving the options reloads the integration and applies the new interval.
A 1-second interval increases HTTP traffic compared with the 5-second default.
Entities do not trigger individual reads or
extra RS485 polling. Descriptor definitions are refreshed when their CRC or the
master UI language changes. Losing the master makes entities unavailable; losing one module affects
that module only. Commands are serialized with polling. A wrong-CSRF rejection
is retried once after session renewal; ambiguous timeouts are never retried.

After a successful command, the coordinator requests an immediate refresh.
Dashboard cards only consume HA states; they do not create additional requests,
polling timers or service writes. MQTT currently publishes master state every
350 ms, and module state is staggered by at least 25 ms between modules. These
are publication intervals, not guarantees about device-wire sampling or response
time; bus polling, network conditions and update/discovery activity still matter.

Continuous is a runtime setting: Home Assistant does not restore it on startup.
The integration sends only deliberately changed settings, not periodic writes.

## Automatic display backlight shutdown

Master 2.0.55 and updated display firmware expose the same per-display settings in
the master module card, the display System page, MQTT discovery and this integration:

- **Backlight off**: Off (disabled), Only in power saving, or Always.
- **Backlight delay**: dropdown with 1 to 60 minutes since the last touch; default 5 minutes.
- **Backlight**: read-only indication of whether the backlight is on.

Automatic shutdown is disabled by default. It is separate from screensaver
dimming and does not put the display or its network/bus connection to sleep.
When the backlight is off, the first touch only switches the light on; the screensaver
stays visible. Release and touch again to leave the screensaver. Both wake touches
are consumed without pressing a control. Firmware updates
and connection setup keep it awake. Changing brightness or the shutdown settings
restarts the timer; ordinary master status updates do not. Normal brightness is
preserved. Settings are stored on the display only when changed.

Required display versions: 320x480 1.4.5RC, 800x480 1.4.12RC, and ST7796 320x480
1.0.15RC or newer. Older display firmware retains its existing controls but does
not expose these additional entities. Update both the master and the display;
updating only the Home Assistant integration cannot add the firmware feature.

## Security and migration from MQTT

Use this only on a trusted local network. The current master uses HTTP Basic
authentication without HTTPS: traffic, including web credentials, is **not
encrypted**. Do not expose the master to the public internet. Diagnostics omit
passwords, CSRF tokens, hosts, serial numbers and aliases. Home Assistant still
stores the web login in its config entry, so protect its configuration and backups.

MQTT can remain enabled for other applications. To avoid duplicate Home Assistant
entities, disable the master's Home Assistant MQTT discovery and remove old MQTT
devices only after confirming that automations have been migrated.

## HACS packaging

This dedicated repository contains `hacs.json` and one integration under
`custom_components/`. Add
`https://github.com/IceCube20/Open-Fume-Extractor-Home-Assistant` to HACS as an
**Integration** custom repository. It has not been submitted to the default HACS
catalogue. See [the installation guide](../README.md) for details.

## Testing

Run `python -m pip install -r requirements-test.txt`, then
`python -m unittest discover -s tests -v` for local
transport/model tests and coordinator contract tests with lightweight HA stubs.
These stubs do not replace an actual Home Assistant installation. Live installation and automation tests in Home Assistant
are still required before publishing a stable release.

Cross-firmware API/MQTT and browser card/editor contract tests remain in the
[firmware repository](https://github.com/IceCube20/Open-Fume-Extractor/tree/main/HomeAssistant/tests).
They do not substitute for testing the actual HA frontend. Rebuild this
repository's manual installation ZIP with `python tools/build_package.py`; it
excludes tests, caches and firmware/private build settings.
