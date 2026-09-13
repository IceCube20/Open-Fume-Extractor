# Module Guide

OFE modules connect application-specific equipment to the 250 kbaud master bus.
Every module has its own firmware, serial number, persistent settings and status
LEDs. Use the exact firmware target listed in [FIRMWARE.md](FIRMWARE.md).

## Common commissioning workflow

1. Disconnect the module from the OFE bus and attached equipment.
2. Flash the matching `-merged.bin` image at address `0x0`.
3. Check the 115200-baud serial log for the expected module name and firmware.
4. Wire A, B and GND to the OFE bus and reconnect power.
5. Scan and run address assignment from the master's **Bus Diagnostics** page.
6. Open the module card and set a useful alias.
7. Configure its local device bus, GPIOs or profile.
8. Test inputs, outputs, fault behavior and restart persistence.

Normal `.bin` files are signed OTA packages. Merged files are for local USB
installation only.

## Address families

| Range | Module family |
|---|---|
| `0x10`-`0x1F` | JBC FAE Bus and JBC USB |
| `0x20`-`0x2F` | Fan/IO and Fan/IO Pro |
| `0x30`-`0x3F` | Weller Zero Smog |
| `0x40`-`0x4F` | Displays |
| `0x50`-`0x5F` | Universal RS232 |
| `0x60`-`0x6F` | Modbus RTU |

Factory-address devices are identified by module type and serial number during
assignment. Do not manually give two connected devices the same final address.

## Module cards and aliases

The master retrieves module identity, firmware, capabilities, telemetry, local
bus state and configurable entities. Aliases for module I/O are stored by the
module and are reused by the web UI, displays, MQTT and Logic Designer.

An OFE module being online does not guarantee that its attached station, serial
device or Modbus slave is online. The module card and Bus Diagnostics show these
two transport levels separately.

## JBC FAE Bus

The JBC FAE Bus module monitors a compatible station's FAE connection and
provides Work/Stand state to the master. Once its handshake is established,
Stand is inferred while Work is off so both states remain meaningful.

Commissioning checks:

- station type and address are detected
- Device ID and connection flags are stable
- removing a tool starts extraction immediately
- removing another tool during after-run restarts active extraction without
  waiting for the old timer to expire
- disconnecting the station produces the expected warning

## JBC USB

The JBC USB module is an ESP32-S3 USB host for supported station classes through
their CP210x interface. It discovers ports, station identity, operating state,
counters and supported settings. Unsupported controls are hidden when the
station class changes.

Use a protected, current-limited 5 V VBUS supply. The module must never feed an
unprotected USB device from an undersized board regulator.

Depending on station capability, the module can expose temperature, temperature
levels, flow, cleaner settings, time-to-stop and future/hibernation timing.
Station-name writes temporarily use the station's configuration connection mode
and return it to monitoring afterward.

After changing the attached station class, allow descriptor reconciliation to
remove obsolete display and MQTT entities.

## Weller Zero Smog

This module controls and monitors a supported Weller Zero Smog extractor over a
local serial connection. It exposes fan enable, power, RPM, light, filter state
and runtime where supported.

The physical Weller connection requires the correct electrical interface. Use a
MAX3232 for RS232 voltage levels; never connect those levels directly to the
ESP32 UART.

## Fan/IO and Fan/IO Pro

Open **Hardware configuration** from the module card. The editor creates a
pending configuration and commits it only after validation, so temporary GPIO
selections do not immediately disturb the running hardware.

### Main fan output

Choose one of these output modes:

- Relay
- PWM
- Relay and PWM

Configure the relay/PWM GPIOs, active polarity, PWM push-pull or open-drain
drive, PWM frequency, minimum/maximum power and tachometer input. Supported PWM
frequency is 10 Hz to 30 kHz. The editor marks unavailable or reserved GPIOs and
shows their current purpose.

At least one usable relay or PWM path is required while the main fan output is
enabled. Disabling the main output removes the module from the master's list of
selectable extraction outputs.

### Additional channels

Digital inputs and outputs can be added, removed and renamed. Their aliases are
stored in the module. Configure GPIO, active polarity and channel behavior, then
commit and verify every channel from the live module card.

The classic ESP32 used by Fan/IO and Fan/IO Pro does not provide internal
pull-up or pull-down resistors on GPIO34 through GPIO39. An input assigned to one
of these pins must use a suitable external pull-up or pull-down resistor; without
one, an open contact can leave the signal floating. These pins are input-only
and cannot be used as relay, PWM or other output channels.

Short input pulses are edge-latched by the module/master path so they can be
used reliably by Additional Rules and Boolean logic.

### Fan/IO Pro filter monitoring

Fan/IO Pro supports these filter modes:

- Off
- Pressure sensor
- Runtime
- Pressure sensor and runtime

For pressure monitoring, select the sensor type, GPIO/I2C settings and calibrate
clean/warning/full reference points with the sensor installed in the real air
path. For runtime monitoring, enter the replacement interval in days, hours and
minutes and reset runtime only after replacing the filter.

Pressure and runtime alarms are evaluated independently and then combined. A
healthy pressure reading therefore cannot clear an active runtime warning, and
resetting runtime does not hide a real pressure fault.

## Universal RS232

Universal RS232 uses a text profile to describe UART framing, protocol,
poll/match/set frames, value conversion and entities. It can represent up to 32
profile entities and supports repeated on/off commands for devices that require
periodic control frames.

Use TTL wiring only for TTL devices. Add a MAX3232 for true RS232. Test the local
trace before assigning `main_input`, `main_output_enable` or
`main_output_power` roles.

See [PROFILE-BUILDER.md](PROFILE-BUILDER.md) for the complete workflow and
examples.

## Modbus RTU

The Modbus module is an RTU master on a local RS485 bus that is separate from
the OFE bus. Its profile defines slave address, polling interval, register,
function, scaling, value mapping and write behavior for each entity.

Use zero-based protocol addresses unless the device documentation explicitly
says otherwise. Verify register reads and writes with a known slave or PC
simulator before connecting real machinery.

After transferring a profile, reload the descriptor and polling settings from
the module and confirm `descriptor_truncated=0` and `profile_active=yes`.

## Displays

Three display targets are supported:

- AXS15231B/QSPI 320x480
- ST7796/FT63x6 320x480
- Guition JC8048W550 RGB 800x480

Their firmware images are not interchangeable. Displays can communicate by
RS485 or authenticated WiFi and can automatically fail over between transports.
See [DISPLAYS.md](DISPLAYS.md).

## Firmware updates

Select the target module on the master's **Updates** page before choosing the
normal signed `.bin`. Confirm that the detected target and version match the
connected device.

During an update:

- keep module and master power stable
- do not disconnect the active transport
- watch transfer speed, remaining bytes/time and bus quality
- use **Cancel update** only once, then wait for the abort acknowledgement
- confirm the module returns online with the new version

If a failed update leaves a module unbootable, flash its matching merged image
locally over USB.

## Module acceptance checklist

- correct firmware target and version
- unique address and persistent alias
- OFE bus online with stable current-miss count
- local device bus online when applicable
- all configured inputs and outputs operate with correct polarity
- main-output enable, power feedback and RPM are plausible
- filter and device alarms reach master, displays and MQTT
- settings and profile survive a restart
- OTA update and abort behavior have been tested
