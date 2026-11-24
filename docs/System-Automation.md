# System Signals and Actions

Feature versions: master 2.0.91RC; QSPI 1.4.35RC; S3 RGB 1.4.51RC;
ST7796 1.0.45RC; P4 1.0.14RC. See the [firmware index](FIRMWARE.md)
for signed OTA packages and merged USB images.

Routing rules and boolean definitions share the same additional signal catalogue.
The existing extractor, suction-level and main-output-selection signals remain
unchanged. None of these new system inputs is offered as the main extractor input.

The routing and boolean-designer menus use one complete signal catalogue,
including module/channel aliases and descriptor-defined Universal/Modbus entities.
Existing signal IDs are preserved; the designer additionally has its simulation input.

## Boolean Designer Capacity

MCU-S3, MCU-P4 and MCU-P4-PoE support up to **64 blocks and 128 connections per
definition**, with **32 stored definitions**. Inputs and outputs count as blocks.
The runtime's existing limit of 32 distinct output targets shared by active
definitions is unchanged; a single definition exceeding that limit is rejected
with a clear message too. The serialized graph remains limited to 24,000 UTF-8 bytes.

The editor reads the MCU's limits from `/logic/list` and displays live usage.
Adding, duplicating and connecting blocks stops at the corresponding limit with
a clear message. An oversized loaded graph remains editable, but cannot be saved
until it fits; it is never silently truncated. Save requests are also validated
on the MCU before storage or runtime-cache replacement. Existing plans remain
compatible, and runtime allocation now follows the actual block/connection count
instead of reserving the maximum for every definition.

## Designer Connections

Connections snap to the nearest compatible port within a screen-space catch
radius, including when zoomed. The chosen port and the snapped preview are
highlighted. Dragging works in either direction; tapping a source and then a
target is also supported. Escape, pointer cancellation and losing focus cancel
the gesture. Reconnecting an existing wire only replaces it after a valid drop;
cancelling or rejecting the replacement preserves the original wire.

The orthogonal router uses the pinned libavoid-js 0.4.5 WebAssembly library to
plan the graph together, with measured block dimensions and an 18-pixel preferred
lane separation. Assets are served locally from authenticated flash-backed gzip
endpoints, not a CDN. Routing executes in the browser, not on the MCU. See
`assets/vendor/libavoid/README.md` for provenance, licensing and regeneration.
A small existing router remains available if WebAssembly cannot load; the
toolbar reports the limited routing state. Crossings have a small visual gap;
only actual shared branches have junction dots. Blocked routes have a dashed
warning style instead of silently changing the logical connection.

Dragging a block now carries its existing orthogonal routes along, using stable
retained bend anchors. Existing valid lanes are kept rather
than choosing a new shortest path at every pointer position. Unrelated wires are
preserved first; a route is replaced when an obstacle/overlap makes it invalid or
its detour becomes excessive. The grid toggle snaps block/segment moves to the
18-pixel grid; holding Alt bypasses snapping without moving existing blocks.
Loading a definition, clearing it or applying Auto Layout resets this visual route
history. Automatic anchors are browser-only and are not saved in the graph.

Selecting a connection exposes segment handles. Dragging a handle perpendicular
to its segment fixes that path; Escape, pointer cancellation or loss of focus
keeps the previous path. Invalid moves through blocks or over unrelated wires
are rejected. The Properties connection list also exposes the route editor on
mobile. Unlock releases the selected manual path; Reroute recomputes automatic
wires without clearing manual paths. Locked segments participate in global
routing so other connections can route around them.

Only explicit manual routes are saved, as optional link metadata
`route: {p:[[x,y],...],s:[x,y],t:[x,y]}` (2-16 finite bounded orthogonal points).
The runtime ignores this metadata and the graph schema remains 1. Loading strips
invalid metadata without dropping the logical link. Manual paths remain fixed
in the diagram while their terminal segments follow moved blocks; collisions
are shown instead of discarding the stored path. The existing 24,000 UTF-8-byte
graph limit includes manual routes; a rejected save leaves the stored graph
unchanged. Older editor versions do not preserve this optional route metadata.

### Multiple Block Selection

Drag on an empty part of the diagram to select blocks with a marquee. Ctrl,
Command or Shift-click toggles individual blocks; modifier-dragging a marquee
adds to the selection. Dragging any selected block moves the selected group with
one shared displacement, preserving spacing, including at canvas boundaries.
Internal manual wire paths move with the group; external wires stay attached.
Escape, pointer cancellation and loss of focus restore the group's previous
positions and manual routes. The selection itself is never saved in the graph.

The selection toolbar toggle supports touch: enable it to tap several blocks or
draw a marquee, then disable it to drag the group. Normal touch scrolling remains
available outside selection mode. Ctrl/Command+A selects all blocks; Escape or
the clear-selection button clears the selection. Text fields retain their usual
keyboard behavior. Delete removes selected blocks and their incident links;
Duplicate copies selected blocks together with internal links, subject to the
existing node/link limits. Single-block property fields are disabled for groups.

### Multiple Connection Selection

Ctrl/Command or Shift-click toggles individual wires without replacing the current
selection. On touch screens, enable the selection tool and tap the required wires;
the enlarged hit areas make thin connections easier to target. Use the connection
list in Properties when wires are close together. Selected wires can be deleted
or their manual paths unlocked together. Selecting wires alone does not move
blocks or change the logical source/target of a connection.

## Main Output Selection and Activity

**Main output selected** means the module is the configured manual preference.
It remains true while the extractor is off. In automatic mode, the separate
**Automatic** selection input is true instead of an individual manual preference.

**Main output active** means the module is the current assigned main output AND
the extractor output is enabled. It is false while idle, stays true through
afterrun, and returns false when extraction stops. This is the master's control
state, not a proof of physical motor rotation or a tacho feedback signal.

Routing, boolean evaluation, `/state` masks and designer live badges use the same
distinction. For these fixes, only the master requires recompiling/updating.

## New Inputs

| Signal | Scope |
| --- | --- |
| Power saving active | Master: at least one online module is in ECO |
| Alarm / warning / error present | Existing master alarm centre; warnings and errors can coexist |
| Filter change soon / replace filter | Individual supported filter module or any matching module |
| Filter data valid | Individual supported filter module or at least one matching module |
| Module online | Individual registered OFE module, independent of its local device |
| Device connected | Individual JBC, Weller or Universal/Modbus device, or any matching device |
| JBC Stand active | Individual JBC module or any connected JBC station |
| Marker 1 through 8 | Shared runtime boolean signals |

Filter predicates require online, current telemetry. Weller additionally requires
an active local device connection; pressure-based Fan/IO Pro filtering requires a
valid pressure sensor. Missing or stale data is not a healthy filter condition.
Use **Filter data valid AND NOT Filter change soon AND NOT Replace filter** for a
per-module healthy-state indicator. Global validity means at least one valid
filter, not that every configured filter is valid. A boolean NOT alone also inverts unknown/offline
data. The editor shows unavailable filter evaluations as unknown in live mode.

The bridge connection signal uses its existing local-link entity (ID 1), not
descriptor availability. No extra bus polling is introduced.

## New Outputs

| Output | Semantics |
| --- | --- |
| Inhibit power saving | Maintained request: inhibit ECO while HIGH; release when LOW |
| Wake display | Pulse action: wake a specific supported display or all supported displays |
| Keep backlight on | Maintained request for a specific supported display or all supported displays |
| Marker 1 through 8 | Maintained boolean output, also readable as an input |

Multiple producers are OR-combined. Deleting/disabling a rule or definition
releases its request; another active producer still owns its request. These
requests do not overwrite saved ECO, backlight or timeout settings and do not
write to flash. Marker values start LOW after a master restart and are not a
replacement for retained SR/RS block state. Cross-definition feedback is evaluated
on subsequent engine evaluations; do not build un-timed oscillating feedback loops.

Wake actions require a LOW-to-HIGH transition. A newly loaded rule/definition that
is already HIGH must first go LOW. Holding HIGH or repeating a status packet does
not keep waking the display. Offline displays do not receive a deferred wake event.

Display actions require the new display firmware advertising
`CAP_DISPLAY_AUTOMATION` (bit 29). Older displays remain compatible but are not
listed as individual targets for these actions. The compact A3/v1 extension in the
existing DISPLAY_STATUS frame carries a hold flag and a per-display wake sequence;
the existing developer flag and clock trailer retain their offsets. There is no
new command, task, or timer-driven bus polling.

A backlight hold does not exit the screensaver; a wake pulse does. If the master
connection disappears, the hold lease expires after five seconds. The normal
configured timeout then applies. Backlight changes and screensaver wake-ups remain
owned by the existing GUI task. Update, pairing and extractor-related wake behaviour
and touch handling are unchanged.

## Verification

- `node tools/tests/test_main_input_options.cjs`
- `node tools/tests/test_system_automation.cjs`
- `node tools/tests/test_system_automation.cjs --browser` with Playwright installed
- `powershell -File tools/tests/system_automation_native/run.ps1`
- `python -m unittest discover -s tools/tests -p 'test_*.py' -q`

Compiler and native/browser tests do not replace a physical display test.
