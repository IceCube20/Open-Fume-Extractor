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
