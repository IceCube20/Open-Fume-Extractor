# MCU Web Interface: Everyday Use

Applies to MCU-S3 and both MCU-P4 profiles in 2.1.52RC. See the
[MCU guide](MASTER.md) for first setup and the [firmware index](FIRMWARE.md)
for the exact installation image.

## Appearance and Layout

The header's appearance selector offers **Auto**, **Light** and **Dark**.
Auto follows the browser's light/dark preference. The selection is saved in that
browser for that MCU address; it does not change other browsers, display-panel
themes or Home Assistant's theme.

Status uses a consistent responsive layout, with up to five module cards across
on wide screens and fewer columns as space decreases. The module inventory table
keeps the full content width. The Logic Designer uses the wider editing area.
Phone navigation and editor panels remain responsive; long tables can require
horizontal scrolling. A wider desktop does not create additional module slots:
MCU-S3 supports 16 modules, MCU-P4 supports 32.

The native HA overview card has its own one-to-four-column setting. It is not
the MCU's five-card layout and cannot exceed the containing HA section's width.

## Module Cards and Connection Monitoring

Check both transport levels: **OFE online** means the module is reachable;
**device connected** refers to its attached JBC station, Weller, RS232 device
or Modbus slave. One does not imply the other.

1. Find the device in the module inventory at the bottom of Status.
2. Use its **Connection monitoring** checkbox to enable or mute optional
   downstream-disconnection warnings.
3. Confirm the result in the alarm centre and on the virtual EVT indication.
4. Test the required-output case before relying on the installation.

JBC, Weller, Universal RS232 and Modbus support optional connection monitoring.
Muting it does not stop local polling, erase diagnostic counters or make a
disconnected device online. It does not suppress an offline OFE module, an
unrelated hardware/protocol fault, or a disconnected selected/required main
output. Missing main-input/output selection remains a system warning.

From 2.1.52, physical MCU EVT and the virtual EVT shown on Status/Bus Diagnostics
use the same module alarm policy. Optional muted links no longer create a
yellow LED warning absent from the alarm centre. Update the MCU to use the fix;
changing a browser setting alone cannot update physical firmware.

## Additional Rules and Boolean Designer

Additional rules and the designer share the signal catalogue, including aliases,
descriptor-defined inputs/outputs and system states. Configure a meaningful name
on the module before selecting its signal.

Use Additional Rules for a direct input-to-action connection. Use a definition
when gates, latches, timers, pulses or multiple inputs are required. A main input
is not a prerequisite for configuring an independent additional rule.

For a definition:

1. Select its slot, add blocks and assign real inputs and output targets.
2. Connect compatible ports; the highlighted port shows the snapped target.
3. Use Simulation for a dry logic test, then inspect actual inputs in Live mode.
4. Save explicitly and enable only the definitions intended to operate equipment.
5. Verify the real installation with safe loads before normal operation.

On desktop, Ctrl/Command/Shift-click and a selection marquee support groups.
On touch screens, use the selection tool, select the group, then leave selection
mode to move it. Multiple wires can also be selected for deletion or unlocking.
Properties provides a connection list when small touch targets are inconvenient.

Routes avoid blocks where possible. Drag a connection's segment handle to lock
a manual path. **Unlock** releases manual routing; **Reroute** recalculates
automatic paths without discarding locked ones. A dashed route warns of a
visual routing conflict, not a different logical connection. Cancelling a move
preserves the original blocks and connections.

Each definition allows 64 blocks, including inputs/outputs, and 128 connections.
There are 32 stored definitions, a 24,000-byte UTF-8 graph limit and a shared
runtime limit of 32 distinct output targets. Manual route metadata counts
toward the byte limit. The editor and MCU validate limits; they do not silently
truncate an oversized graph.

See [system signals and detailed designer behavior](System-Automation.md).

## Files, Profiles and Updates

On P4, **Install/Load from USB** opens a contextual file picker. Selecting a file
loads it into the existing form; installation, restore and profile transfer
remain separate actions. **Save to USB** asks for the destination folder and
filename. The dedicated USB page is available through navigation and the
connected-stick icon. See [USB storage](USB.md).

The Universal/Modbus and Fan/IO hardware editors keep configuration changes
pending until validated and committed. A JSON/profile import is not proof of
successful communication with the attached device. Check the local trace after
saving and before assigning an extraction role.

For C6 updates, distinguish the MCU firmware, its Hosted driver and the C6
firmware version. Normal users receive only the board-specific approved image.
The Espressif version selector is private-developer functionality, not permission
to assume every upstream version is compatible. See the
[C6 update and recovery guide](Hosted-C6-Catalog.md).

## Quick Checks

- Wrong LED color/alarm: compare the alarm centre, connection-monitor preference
  and actual main-output selection before muting anything.
- HA LED colors correct but no effects on one client: check that client's
  reduced-motion setting. It does not require an MCU firmware change.
- Narrow HA columns: inspect the containing HA layout as well as the card setting.
- Rejected save: read the validation message, check GPIO conflicts or graph/profile
  limits and keep the original saved configuration until the correction succeeds.
