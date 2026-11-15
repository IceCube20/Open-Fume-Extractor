# S3 RGB Sequential OTA Test

Local display firmware: **1.4.43RC**. This is an unverified hardware test change,
not a claim that the observed transfer stall has already been reproduced or fixed.

## Evidence And Scope

The same Master delivered the P4 update at 43.5 kB/s, with 1024-byte bulk packets
and no bulk fallbacks. The S3 RGB update had multi-second ACK waits and failed
after bulk fallback. Its update page remained visible. These observations do not
identify the precise origin of the missing response.

The installed S3 high-performance Arduino SDK's `libraries/Update/src/Updater.cpp`
uses 64-KiB block erases at aligned flash boundaries. The S3 RGB display also
continuously scans a PSRAM framebuffer through internal bounce buffers. Long
erase/write pauses are therefore a candidate to measure, not a proven diagnosis.

## Test Change

Only S3 RGB selects `OfeRgbOta`, a small adapter over Espressif's native OTA APIs.
It starts the inactive OTA partition with `OTA_WITH_SEQUENTIAL_WRITES`. Existing
4096-byte application buffering makes writes and incremental erases sector-sized
instead of using Arduino's aligned 64-KiB block erase optimization.

The transport's offset checks, duplicate handling, ACK ownership, progress view,
timeout, abort and reboot policies remain unchanged. The adapter checks the
expected image size, calls `esp_ota_end` to validate the image, and only then calls
`esp_ota_set_boot_partition`. Failed or incomplete transfers never intentionally
select the new partition for boot. Espressif's image validation is not a replacement
for the OFE cryptographic signature checks already performed by the Master.

Panel timings, graphics and the high-performance SDK are unchanged. P4 and the
two small display ports retain their Arduino update writer. No release binaries
or public repositories are updated by this source change.

## Installation And Measurement

1. Compile and install 1.4.43RC on the S3 RGB display, preferably over USB. An OTA
   transfer to the old running firmware still uses its old writer, even if the
   uploaded image contains this change.
2. Once 1.4.43RC is running, repeat the WLAN update and capture the full Master
   progress line, including `OTA Bulk/OFE`, ACK times and fallback count.
3. If USB logging is available, capture `OTA FLASH: IDF sequential` lines. They
   show cumulative write count, average/max write duration, and the last IDF error.
4. Repeat over RS485 to compare the same writer without the WLAN path.

Native tests cover byte preservation, size bounds, incomplete images, SDK begin/
write/end/boot-selection failures, abort cleanup and timing. Existing transport
tests cover retransmissions, exact completion, timeout and reboot behavior.
Throughput, panel stability and power-loss behavior still require device tests.

Reference: [Espressif OTA API and sequential-write mode](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/api-reference/system/ota.html).

## WLAN Stage Diagnostics: 1.4.44RC

The 1.4.43RC device log showed 69,016 bytes minimum free internal memory, a
31,732-byte largest block and sequential flash writes averaging about 60 ms,
with a maximum of 62.9 ms and no reported IDF write error. During the same
investigation, the RS485 update reached 10.6 kB/s with no retries and a maximum
ACK wait of 79 ms. This supports investigating the WLAN-specific path rather
than attributing the multi-second waits to the measured flash calls or to an
observed internal-memory allocation failure. Neither hardware nor WLAN behavior
is fully diagnosed by these snapshots.

The 1.4.44RC test adds an `OTA WIFI` line next to `OTA FLASH` during WLAN updates:

- `rx`: bulk datagrams dequeued from the expected peer, including rejects.
- `dup`: repeated requests served by the last-request cache.
- `reject`: bulk authentication/session/format rejects.
- `ack-fail`: failed local ACK packet generation/socket-send attempts.
- `poll`: maximum interval between transport receive-poll calls during WLAN OTA.
- `check`: maximum bulk decoding and authentication/session-check duration.
- `handle`: maximum firmware callback or cached-response processing duration.
- `ack-send`: maximum ACK encoding and local socket-send duration.

All four maximum stage durations are in **microseconds**. A successful local
socket send is not proof of delivery over the radio. A long poll interval can
include processing of the previous datagram, including its flash write; compare
it with `handle` and `OTA FLASH`, rather than treating it as pure scheduling delay.

Sampling uses fixed atomic counters (native size check: at most 64 bytes), no
per-packet logging and no extra heap buffer. The protocol, retry policy and ACK
send behavior are unchanged. `OFE_DISPLAY_OTA_TIMING` is enabled only in the S3
RGB sketch. The small ports retain the same shared adapter code with diagnostics
disabled and preserve their existing static WiFi buffers and DNS policies. P4
also retains disabled diagnostics and its original writer.

Install 1.4.44RC on the S3 through the now-working RS485 path or USB. Once it is
running, repeat WLAN OTA and capture matching `OTA WIFI`, `OTA FLASH` and Master
progress lines. This is a diagnostic build, not a claimed throughput fix.
