# Display OTA Task And Buffer Comparison

Source and installed SDK inspection, 2026-10-26. The tables describe the baseline
before the isolated S3 1.4.45RC priority experiment documented below.

## Application Tasks

| Display | Transport task | GUI task | WiFi configuration worker |
| --- | --- | --- | --- |
| S3 QSPI 320x480 | Core 0, priority 2, 8192 B | Core 1, priority 2, 16384 B | Core 0, priority 1, 6144 B |
| S3 ST7796 320x480 | Core 0, priority 2, 8192 B | Core 1, priority 2, 16384 B | Core 0, priority 1, 6144 B |
| S3 RGB 800x480 | Core 0, priority 1, 8192 B | Core 1, priority 2, 16384 B | Core 0, priority 1, 6144 B |
| P4 800x480 | Core 0, priority 1, 8192 B | Core 1, priority 2, 16384 B | Core 0, priority 1, 6144 B |

The transport task polls both the wired link and WLAN tunnel and handles OTA
callbacks. The `display-wifi` worker handles configuration, scans and name
resolution; during OTA it defers those operations. Raising only that worker's
priority would not raise the task executing the OTA receive path.

All four already have a dedicated GUI task. Moving LVGL into a task again is not
the missing change. The GUI runs on the other core, so its priority does not
directly compete with the core-0 transport task. Cross-core locks and shared
memory/peripheral contention can still matter.

## Effective S3 WiFi Configuration

Both installed S3 SDKs inspected here configure eight static RX buffers and 32
dynamic RX/TX buffers in `sdkconfig.h`. This includes the RGB high-performance
SDK (Arduino 3.2.0 / IDF 5.4.1) and the small-panel standard SDK (Arduino 3.3.12).

Arduino's `WiFiGeneric.cpp` changes those defaults when
`WiFi.useStaticBuffers(false)` is selected:

| Setting | Small S3 panels: `true` | RGB S3: `false` |
| --- | --- | --- |
| Hardware/static RX buffers | 8 | 4 |
| Dynamic RX buffers | 32 | 32 |
| TX type | Dynamic | Dynamic |
| Dynamic TX buffers | 32 | 32 |
| Static TX buffers | 0 | 0 |
| TX cache count in init structure | 0 | 4 |

Thus, calling the small-panel configuration "static TX" would be incorrect for
these installed builds. `true` preserves SDK defaults; it does not override the
compiled TX type. The TX-cache count alone does not prove that cache buffers are
active in this dynamic-TX configuration.

Espressif documents static RX buffers as DMA receive memory, approximately
1600 bytes each. Four additional buffers account for about 6400 bytes of raw
buffer storage, before driver overhead and any other configuration differences.
This is an estimate, not a measured heap delta. More RX buffers can help receive
bursts, but consume internal memory and do not guarantee a throughput increase.
See [ESP-IDF 5.4.1 WiFi buffer documentation](https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32s3/api-guides/wifi.html#wi-fi-buffer-configure).

The P4 also calls `useStaticBuffers(false)`, but WiFi is hosted on its companion
chip. Its fast OTA result is not an equivalent test of the S3's local WiFi driver.

## What The Measurements Establish

The S3 OTA log measured a maximum transport poll gap of about 64 ms, including a
maximum flash handler time of about 60.6 ms. Local ACK enqueue was at most about
1.4 ms and reported no send failures. These maxima cover that recorded interval,
not every future stall. They do not show multi-second transport-task starvation,
nor do they prove that locally accepted ACKs reached the Master.

The same RGB display updated over RS485 at approximately 10.6 kB/s without the
WLAN delays. Internal heap during the recorded WLAN update stayed near 70 KiB,
with a largest block near 31 KiB and no reported allocation failure. That does
not prove dynamic allocation is harmless, but it does not show heap exhaustion.

Priority, RX buffering, older SDK behavior and RGB PSRAM/DMA contention remain
test candidates, not established root causes. The common 1024-byte bulk payload
and 188-byte compatibility fallback do not explain a configuration difference
by themselves.

## Controlled Test Order

1. Keep the current Master packet size at 1024 B and record the S3 baseline.
2. Test only transport priority 2, using the existing
   `DISPLAY_RS485_TASK_PRIORITY` override; keep the WiFi buffers unchanged.
3. Return to baseline priority and test only `useStaticBuffers(true)`, set before
   radio initialization. Check boot and OTA free/largest internal heap, actual
   LVGL draw-buffer size, Bluetooth pairing and rendering performance as well.
4. Combine settings only if the separate tests justify it. Test smaller Master
   bulk packets separately, with the display settings held constant.

Do not move all buffers to internal SRAM, change GUI priority, disable validation
or increase timeouts to hide missing replies. Keep a working USB/RS485 recovery
path and compare the same image, AP and other system load.

## Isolated S3 Test: 1.4.45RC

Only the S3 RGB bus/WiFi transport task default changes from priority 1 to 2.
The generated P4 port explicitly keeps priority 1. The WiFi configuration worker
remains at priority 1; its job is not OTA reception. A new S3 startup line reports
the actual running transport task priority, stack size and active/idle delays.

WiFi buffers, bulk size, poll budget, delays, OTA writer, GUI task, panel clocks,
draw buffers and appearance remain unchanged. The existing explicit
`DISPLAY_RS485_TASK_PRIORITY` build override remains supported for rollback.

Install only the S3 RGB test build. Keep the Master firmware and its packet-size
selection unchanged for the comparison. Installing this firmware through OTA
still uses the old running firmware's transport task; measure a subsequent update
after the new version has booted. Confirm `1.4.45RC` and, if serial is available,
`OFE bus/WiFi task: priority=2` before evaluating the next transfer.

Record sustained speed, retries/fallbacks and ACK timing, not just the first
progress sample. Check that touch, rendering and Bluetooth pairing remain
responsive. This is a controlled test, not a confirmed throughput fix. If it
does not improve the result, test RX buffers separately rather than adding
unrelated scheduling changes to this comparison.

## Priority Test Result And S3 Buffer Test: 1.4.46RC

The user reported no improvement with the priority test. The update aborted at
79036 bytes. The Master counted 115 bulk send attempts, 85 ACK-shaped replies,
41 ACK timeouts and 11 replies for other request IDs. Invalid/authentication,
endpoint, session, offset and device-status rejection counters were zero; its
maximum bulk polling gap was 8.6 ms. These figures show repeated unsuccessful
ACK waits, not a measured multi-second Master receive-task stall. They do not
locate where requests or replies were lost or delayed.

Version 1.4.46RC restores the RGB transport task's baseline priority 1 and tests
the SDK-default WiFi buffer profile instead. This retains eight static RX
buffers rather than Arduino's reduced four. TX remains dynamic in the installed
SDK. The SDK-default TX-cache init field also differs, as shown in the table;
this is a profile comparison, not an RX-count-only driver modification.

`OFE_DISPLAY_WIFI_SDK_BUFFERS=0` explicitly restores the previous Arduino profile
for an A/B build. The default is 1 only for the S3 RGB header. The generated P4
port defaults to 0 and retains its established hosted-WiFi behavior. The small
displays already use SDK defaults and are not changed.

The new S3 startup line prints the SDK static/dynamic RX counts and TX type. The
existing startup and heap logs remain available. Expect approximately 6400 bytes
of additional raw static RX storage, not an exact measured total heap delta.
The existing internal-memory draw-buffer reserve guards remain unchanged and
may select a smaller draw tile if required; record the actual LVGL buffer size
as well as free/largest internal heap and Bluetooth pairing behavior.

Keep the same Master firmware and 1024-byte bulk setting. Install through a
working USB/RS485 route, confirm 1.4.46RC after reboot, then perform a subsequent
WLAN OTA test. Poll cadence, batch limits, OTA writer and validation, graphics,
panel timing and GUI scheduling are otherwise unchanged. No speed improvement
is claimed before that hardware measurement.

## Buffer Test Hardware Result

The supplied log contains both the earlier priority-2 boot and the subsequent
SDK-buffer-profile boot with static RX=8, dynamic RX=32, TX type=1 and transport
priority=1. The latter confirms that the buffer experiment actually ran. The
early LVGL SRAM reservation fell from 48 lines (76800 B) to 40 lines (64000 B);
internal RAM immediately after LVGL initialization was 109092 B, with a largest
block of 47092 B. These are startup measurements, not OTA heap measurements.

The user still measured approximately 4.1 kB/s. The recorded OTA interval shows:

- Flash writes averaging approximately 58 ms and reaching at most 62.4 ms,
  with error=0.
- Maximum transport polling gap 65060 us and maximum bulk handler time 62504 us.
- Maximum local ACK enqueue time 1486 us; ack-fail=0 and reject=0.
- 626 received bulk requests and 58 cached duplicate requests at the last sample.
  This is not an end-to-end packet-loss percentage.
- Intervals with no new received bulk request while polling and the GUI continue.

The measured callback and local ACK-enqueue durations do not account for the
multi-second waits seen at the Master. Duplicate authenticated requests show
that at least some retransmissions reached the display, but do not establish
where the original ACK was delayed or lost. Successful socket enqueue is not
proof of delivery. No simultaneous final Master counter sample was supplied,
so totals from different points must not be subtracted to infer loss direction.

The buffer profile has not demonstrated a benefit and should not be promoted
as a performance improvement, particularly given the smaller render tile.
Temporarily keep this running display build unchanged for a size comparison:
on Master 2.0.78RC-Developer, test the same image and display with 1024 B and
512 B selected under `WiFi packet size (test)`. Keep all other conditions fixed.
Use a reference measurement on that same Master version first. If stopping a
trial early, use the update page's abort button and wait for cancellation before
starting another trial. Capture complete progress lines from both sizes.

The Master selector is a diagnostic experiment, not a confirmed fix; 1024-byte
payloads do not by themselves imply IP fragmentation. No additional display
firmware build is needed for this comparison. The S3's older IDF/WiFi driver,
radio conditions and shared RGB/external-memory traffic remain candidates, not
proven causes.
