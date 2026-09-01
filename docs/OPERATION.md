# Operation and Diagnostics

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

## Status LEDs

| Effect | Meaning |
|---|---|
| green/white breathing | OFE bus online |
| short green flash | bus traffic |
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

Global brightness is configured under **Network Setup > Status LEDs**. USB-powered
prototype hardware may behave better below 100% brightness when the cable,
protective diode or supply has noticeable voltage drop.

## Boolean Logic Designer

The graphical designer supports real module inputs, simulation inputs and outputs,
including AND, OR, NOT, RS latch, Toggle, TON, TOFF, Pulse and Clock blocks. Up to
32 definitions can be stored and enabled independently. Fan/IO aliases stored in
the module are used as signal names.

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

