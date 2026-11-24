# Operation and Diagnostics

For current theme, desktop/phone layouts and editing workflows, see the
[web interface guide](WEB-INTERFACE.md). P4 file operations have a separate
[USB storage guide](USB.md).

## Main signal path

The **Main extraction input** decides when extraction is requested. The
**Main extraction output** selects the module that controls fan enable and power.
Auto output selection chooses a suitable online module. A manually selected
offline module falls back to no active output while its former selection remains
visible for diagnosis.

## Extraction settings

- Low, Medium, High or User power level
- configurable user power
- separate Work and Stand after-run time
- optional lower after-run power
- Continuous mode for intentional input-independent operation

Settings are persisted with delayed/coalesced writes to avoid unnecessary NVS
wear while sliders are moving.

## Alarms

The master is the authoritative alarm source for web UI, displays and MQTT.

Warning examples:

- no main extraction input or output selected
- JBC station or Weller device link disconnected
- filter approaching its limit

Critical examples:

- remembered OFE module offline
- output/tachometer failure
- critical station or local-device-bus fault

Yellow indicates a warning; red indicates a critical fault.

The module-list Connection monitoring checkbox controls optional downstream
link warnings for JBC, Weller, Universal RS232 and Modbus. An OFE node going
offline remains an error. A disconnected selected/required output remains an
error even when the optional connection warning is muted. Muting does not stop
polling, erase diagnostics or suppress unrelated faults.

Universal/Modbus reports local link loss when no device has replied. Repeated
response timeouts are a separate diagnostic for an established responding link;
they are not an extra alarm solely because no device is installed.

From 2.1.52, the MCU's physical and virtual EVT use the shared alarm policy.
Compare the alarm centre and intended output selection before changing a
monitoring preference.

## Status LEDs

| Effect | Meaning |
|---|---|
| green/white breathing | OFE bus online |
| short green flash | module bus activity where enabled; MCU communication flashes are intentionally disabled |
| red breathing | OFE or local device bus offline |
| orange blinking | not paired or invalid address |
| blue/white breathing | firmware update |
| white breathing | local device online |
| solid green | JBC Work active |
| blue breathing | extraction active |
| violet blinking | after-run |
| blue blinking | Continuous mode |
| yellow blinking | warning |
| red double blink | critical alarm |

Global brightness is configured under **Network/System > Status LEDs**. USB-powered
prototype hardware may behave better below 100% brightness when the cable,
protective diode or supply has noticeable voltage drop.

HA card effects respect the client's reduced-motion preference. Static colors
with correct states on one client are not, by themselves, an MCU fault. On
Windows, check Accessibility > Visual effects > Animation effects, then reload
that HA page. Preserve reduced motion if it is required for accessibility.

## Boolean Logic Designer

The graphical designer supports real module inputs, simulation inputs and outputs,
including AND, OR, NOT, RS latch, Toggle, TON, TOFF, Pulse and Clock blocks. Up to
32 definitions can be stored and enabled independently. Fan/IO aliases stored in
the module are used as signal names.

A definition allows 64 total blocks, 128 connections and 24,000 UTF-8 bytes;
32 distinct runtime output targets are shared by active definitions. Save errors
leave the stored graph unchanged. The designer supports grouped block moves,
multiple wire selection and explicit manual wire segments. See
[designer controls and limits](System-Automation.md).

## Bus Diagnostics

Important values:

- bus load, TX/RX rate and frames per second
- current and maximum response latency
- timeouts, sequence and CRC/transport errors since boot
- historical and consecutive live misses
- separate OFE-bus and local-device-bus state

Historical counters can include an intentional disconnect. Current online state,
consecutive live misses and continuously increasing counters are more important
than a non-zero historical total.

Firmware-transfer timeouts and restart-wait timeouts are shown separately from
normal polling timeouts. The normal timeout percentage excludes these maintenance
requests. After a successful update, the target has up to 15 seconds to reconnect;
a valid INFO response ends this window early. Other modules remain monitored.
Raw CRC, LEN, ESC and protocol errors are still recorded during updates.

## MQTT and Home Assistant

The master publishes itself and each module as separate Home Assistant devices.
Language, module aliases, alarms and dynamic Universal/Modbus entities follow the
master configuration. Availability is published when MQTT is disabled or the
master disconnects, and obsolete Discovery entities are removed after profile or
station-class changes.

## Backup and restore

The JSON backup contains network credentials, web login, MQTT credentials and CA
certificate, routing, extraction settings and logic definitions. Treat it as a
sensitive file. A backup is only applied during an explicit restore; it is not
automatically reloaded on every normal boot.
