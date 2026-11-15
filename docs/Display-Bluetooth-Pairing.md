# Direct Bluetooth Display Setup

Recommended Master build: **2.0.62RC** or newer. The Bluetooth memory fixes require
Display 320x480 **1.4.11RC**, Display 800x480 **1.4.18RC** or Display ST7796 320x480
**1.0.21RC**, or newer versions of those display builds.

These builds fix an SDK configuration conflict in the first Bluetooth builds:
Arduino's SDK defaults selected internal memory and three connections despite the
original command-line settings. This could exhaust the Master's internal RAM.

Bluetooth is a temporary setup channel between the Master and a display. It
does not replace the normal RS485/WiFi connection, and no phone is required.
The Master must already be connected to the WiFi network the display will use.

## Pair Without an RS485 Cable

1. On the display, open **System > Connection > Pair via Bluetooth**.
2. Open the Master's web interface and click the Bluetooth icon in the header.
   Click **Find displays**, then **Pair** next to the matching serial number.
3. Compare the six-digit code in the web interface with the code on the physical
   display. Confirm **Code matches** on both devices only if the codes agree.
4. Wait for **Display paired**. The display saves the Master's WiFi credentials,
   address and its individual pairing key, then uses the existing WiFi transport.

The Master repeats short scans throughout its two-minute search window, so a
display switched into pairing mode later can still appear. **Search running:
00:23 / 02:00** shows elapsed time and the search limit; reloading the page does
not reset it. The timer remains visible between scan cycles and disappears when
pairing begins or the search ends.

Up to six displays are retained in the result list. After a successful pairing,
that entry is marked **Paired** and another discovered display can be selected
without starting a new search. Only one display is paired at a time. Each display
must still be in its own active pairing window when selected.

Setup expires after two minutes. Cancel or reject a code to stop it. Bluetooth
is shut down after completion, failure, cancellation or expiry. The header icon
is blue only while the local pairing window is active; it is not a WiFi data link.
The existing RS485 provisioning and manual WiFi/pairing-code setup remain available.
The connection panel, keyboard and confirmation dialogs leave the live header
visible. Its right-hand indicators are alarm/count, Eco, Bluetooth and WiFi/serial;
each has a reserved position even when inactive.

The connection panel separates the current connection from setup feedback. WiFi
and Master settings use two columns on the 800x480 display and a scrollable
single column on the smaller displays. Controls have consistent 44-pixel touch
targets and follow the selected language and theme. The Bluetooth button changes
to **Cancel Bluetooth** during an active pairing window. Unsaved field edits are
not replaced by background status refreshes.

## Memory During Pairing

The NimBLE host allocator already uses PSRAM, without an internal-memory fallback.
Active Bluetooth still requires internal RAM for the controller and RTOS resources;
it cannot operate entirely from PSRAM with the bundled Arduino SDKs. Do not move
Bluetooth or flash-writing task stacks to PSRAM or override internal allocations.

The Master now processes scan advertisements immediately and retains at most six
matching OFE identities, not a list of unrelated nearby devices. It scans for up
to four seconds, then releases the radio for up to three seconds before the next
scan, retaining the result list throughout. Selecting a display during a scan
starts pairing after that scan finishes. It restarts the radio for the secure
connection. Both sides use a smaller 20-entry controller duplicate cache. This
cache size does not limit the number of devices the scanner can encounter.

A display must keep advertising while it waits to be paired, so its internal heap
still drops temporarily. Completion, cancellation and expiry stop Bluetooth and
release its temporary resources. The exact heap change must be measured on the
hardware; a fixed 120 KiB during active pairing is not guaranteed. The minimum-heap
reading remains low after recovery because it is a historical low-water mark.

Master 2.0.60RC also moves the fixed 96-address bus-diagnostics table from internal
static storage to PSRAM: 24,576 bytes with the current ESP32 ABI. All counters and
timeout observations remain available. An internal allocation is used if PSRAM
is unavailable. This is separate from Bluetooth's temporary allocations; actual
free-heap readings still depend on active tasks and network connections. At boot,
`[MEM] Bus diagnostics: 24576 bytes in PSRAM` confirms successful placement.

## Forget a Master

Open the connection panel on the display, select **Forget master pairing** and
confirm. The Master address and pairing key are deleted after verified persistent
storage. The WiFi SSID and password remain saved. Automatic configuration from the
RS485 Master is disabled until explicitly re-enabled with **From master**.

This deletes the pairing on that display. It does not delete its remembered device
entry on the Master and does not prevent it from operating over a physical RS485
connection. Remove unused device entries on the Master separately.

## Build and Verify

- Install **NimBLE-Arduino 2.5.1** using Arduino's Library Manager. Use the normal
  project build options, including OPI PSRAM and the large display's existing
  high-performance SDK. Do not change RGB timing or DMA buffers for Bluetooth.
- Keep both `build_opt.h` and `OfeBleBuildConfig.h` in each sketch folder. The
  build options force the project configuration into every C/C++ compilation
  after loading SDK defaults. NimBLE then allocates its host pools in PSRAM and
  uses one connection. Editing the installed Arduino SDK/library is unnecessary.
  Incompatible or omitted build settings now produce a compilation error.
- BLE is initialized only on demand. Its controller and some task/event resources
  still need internal RAM. The safety guard remains unchanged: 56 KiB free and a
  16 KiB block before radio startup, with at least 20 KiB remaining after startup.
  A rejected Master setup reports its last memory check in the web interface.
- WiFi credentials are sent only after encrypted, authenticated BLE pairing with
  numeric comparison. The HTTP setup controls use the existing web authentication
  and CSRF protection. Use a trusted local network for the Master's web interface.
- After flashing the new Master and display firmware, test pairing, cancellation,
  power-cycle persistence, forgetting, and RS485/WiFi OTA on the actual hardware.
  Automated compiler, protocol and UI tests cannot validate radio or panel timing.
