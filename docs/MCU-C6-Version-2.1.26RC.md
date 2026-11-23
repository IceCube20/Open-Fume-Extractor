# MCU 2.1.26: Deferred C6 Firmware Version Read

A working WLAN scan proves that the C6 transport can communicate. A missing
version is not evidence that the chip is absent. Previously the Hardware page
only used the Arduino version cache; our own cache was populated only during
a C6 update. A failed early SDK read could therefore leave the version blank.

The HTTP service now schedules a one-shot FreeRTOS worker when the cached
version is unknown and the C6 transport is already active. It refreshes the
Arduino SDK cache with `hostedHasUpdate()` and caches a validated result for
our Hardware and C6 status pages. The direct read supplies a diagnostic error
if the SDK cache remains empty. `hostedHasUpdate()` only checks versions; it
does not install or download firmware.

- HTTP rendering remains cache-only; no blocking RPC is added to a page GET.
- No radio initialization, reset, WiFi mode change or firmware write is added.
- Only one query runs at a time. WiFi shutdown and conflicting configuration,
  pairing or firmware operations wait for the query to finish.
- Active scans, pairing and MCU/module/C6 updates prevent new queries.
- Failed reads use a 30-second retry interval after the worker completes, so
  even a slow failing RPC leaves a gap for other services. Task allocation
  failures also use a 30-second backoff while the
  existing transport is active. Unknown/zero/oversized versions stay unknown.
- A successful read is retained after WiFi suspension. It also rechecks an
  existing post-update verification journal against the actual returned version.
- `/update/c6/status` exposes `version_probing` and `version_error`; serial
  output uses `[C6] Firmware version:` or a query-error line.

This does not approve the PoE board in the public C6 update catalog and does
not assume its installed version equals the host SDK's 2.12.13. If a factory
firmware does not support the version RPC, the new diagnostics report that
failure rather than inventing a version or automatically updating the C6.

After installing the MCU firmware, run a WLAN scan if necessary, then reload
Network/System. The C6 itself does not need to be flashed merely to try this fix.

Native regression scenarios reproduce a cold SDK cache with a functioning
transport, deferred execution, SDK cache refresh, retention after WiFi-off,
single flight, update exclusion, failed/zero/oversized replies, task allocation
failure, backoff and time rollover. They assert no radio reset or firmware write.

## Clock Initialization On Ethernet/WiFi

The user also reported an unset clock on the V3.2 board while the older P4
has a working clock. The PRE-V3 and V3 SDK configurations for DNS, SNTP and
system time are identical. That comparison does not establish the device-side
cause. Previously our code started SNTP only once, even across network changes.

The shared MCU network service now starts asynchronous SNTP after a route has
been stable for two seconds. It restarts on an interface/IP change or reconnect,
and retries every 60 seconds only while the system time is still invalid. A
valid clock retains the SDK's normal periodic synchronization. No blocking
`getLocalTime()` wait, hardcoded server IP, radio startup or SDK change is added.
Pairing and firmware operations defer new starts. LAN-only startup without an
SSID is covered. Serial `[Time] NTP start via ...; DNS ... / ...` reports the
chosen transport and current DNS servers; it does not claim a successful reply.

Native tests exercise both actual WiFi service paths plus route settling,
LAN-only boot, reconnects, same-IP interface changes, DHCP address changes,
missing time replies, valid time, busy guards and `millis()` rollover. Hardware
verification of the user's clock and C6 display is still required.

Espressif documents global DNS interactions with multiple interfaces:
<https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/protocols/lwip.html#when-both-wi-fi-and-ethernet-are-enabled-and-a-static-ip-is-set-for-ethernet-dns-resolution-fails-how-should-this-be-troubleshooted-and-resolved>
This is a diagnostic possibility, not a confirmed cause on this board.

## Verification

- 375 Python tests passed, including the existing MCU source regressions.
- Actual S3 and P4 WiFi service native fixtures passed, including NTP lifecycle.
- P4 native tests passed, including the slow-failing C6 query, revision guards
  and pinned C6 image staging/hash tests.
- PoE V3 Arduino build completed; no device was flashed and nothing published.
- Verified app: `2.1.26RC-Developer`, 3,566,352 bytes, 32 MiB flash profile,
  revision range 301..399 in both app and bootloader; merged/export copies match.
- App SHA-256:
  `f0e36b80ce06c10907ccc940d0cb5616b26282efea182cd6748e293bbb27e334`.
- Actual ELF/BIN ECC/RNG link inspection passed; old SDK ECC is excluded.
- PRE-V3 and PoE generated-source manifests both match the canonical sources.

The OTA file is
`tools/tests/build-checks/master-p4-poe-v3/OpenFumeExtractorMCU_P4_POE.ino.bin`.
Use only on the matching PoE V3 board. Device-side validation of C6 version and
clock recovery remains pending. If time stays unset, capture `[Time]` alongside
`[Network]` and `[C6]` lines after startup; these tests do not emulate a DNS/NTP
server or establish the cause on the physical board.
