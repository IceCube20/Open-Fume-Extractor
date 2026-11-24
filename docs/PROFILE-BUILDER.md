# Universal RS232 and Modbus RTU Profile Builder

Profiles describe devices whose protocol is not compiled into a dedicated OFE
module. The master transfers the profile to the Universal RS232 or Modbus RTU
module, where it is stored persistently. Web UI, displays, MQTT and the Logic
Designer consume the same resulting descriptor.

## Limits

- 32 profile entities per module
- 8192 bytes of profile text
- profile entity IDs start at 20
- system/debug entities remain available independently

## Basic workflow

1. Open the Universal RS232 or Modbus RTU module card.
2. Open the Profile Builder or expert text view.
3. Import a profile or create one from the device manual.
4. Transfer it to the module.
5. Reload the descriptor and verify `descriptor_truncated=0` and
   `profile_active=yes`.
6. Test read and write operations before assigning an extraction role.

### P4 USB Import/Export

Load from USB selects a profile into the editor, not directly into the module.
Validate and transfer it explicitly. Save to USB chooses the destination folder
and filename for the expert profile, Protocol Builder or Register Map export.
The dedicated [USB file manager](USB.md) handles folders and drive selection.
On a phone, use the editor's header close control and scroll inside the editor;
file import/export is kept separate from the form's commit action.

## Common profile header

```text
profile=Display name
station=Device name
baud=9600
frame=8N1
protocol=...
checksum=...
```

Universal profiles may also define `line_end`. Modbus profiles use a slave address
and default polling time.

## Entity example

```text
entity.1.type=number
entity.1.name=Power
entity.1.key=power
entity.1.access=rw
entity.1.role=main_output_power
entity.1.unit=%
entity.1.min=30
entity.1.max=100
entity.1.step=1
```

Supported entity types include `binary_sensor`, `sensor`, `number`, `switch`,
`select` and `text`. Access is `ro`, `wo` or `rw`.

## OFE roles

| Role | Purpose |
|---|---|
| `main_input` | selectable extraction or logic input |
| `main_output_enable` | extraction output on/off |
| `main_output_power` | extraction power setting |
| `output_enable` | additional switchable output |

## Universal RS232 example

```text
entity.1.poll=S
entity.1.match=S###
entity.1.set=d{value:03}
```

`{value:03}` formats a three-digit value. Binary payloads can be configured as
hex fields. Devices requiring repeated on/off commands can use `repeat_on_ms` and
`repeat_off_ms`.

Time conversion and display formatting are part of the descriptor:

```text
entity.5.scale=10
entity.5.time_base=m
entity.5.time_display=dhm
```

## Modbus RTU example

```text
entity.3.reg=0x0001
entity.3.func=write_holding
entity.3.read_func=read_holding
entity.3.poll_ms=500
```

Typical functions are `read_discrete`, `read_coil`, `write_coil`, `read_input`,
`read_holding` and `write_holding`. Use the actual zero-based protocol register,
not a manual's display notation such as 30001 or 40001 unless the device manual
explicitly states otherwise.

## Local Link and Timeout Diagnostics

OFE-online confirms communication with the bridge, not with the serial/Modbus
device. A never-connected or expired local link is shown as local device-bus
loss. Repeated answer timeouts are separate from the absence of a device;
transient/trailing counters can remain as history after recovery.

Connection monitoring controls only optional link warnings in the MCU alarm
centre. It does not stop profile polling or make old values current. A selected
required output must still report device-bus failure even when warnings are
muted. Do not use stale feedback as evidence of safe operation.

## Testing

1. Connect a known Modbus slave or PC simulator.
2. Populate discrete inputs, coils and registers with known values.
3. Start the local-device-bus trace.
4. Verify reads, writes, response function codes and CRC.
5. Compare the processed value in web UI, display and MQTT.
6. Disconnect the local bus and verify fault, alarm and LED behavior.

After changing a profile, the master removes entities that no longer exist from
its caches and MQTT Discovery. Reload the descriptor before rebooting devices.

