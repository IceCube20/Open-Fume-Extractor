# S3 RGB WLAN OTA Isolation

Local S3 test firmware: **1.4.49RC**. Not built, flashed or published by this
source change. P4 and small-panel behavior is unchanged.

## Why This Test

The user reports that transport priority 2, the SDK-default WiFi buffer profile
and smaller bulk packets did not resolve slow S3 RGB WLAN OTA. A P4 using Master
2.0.78RC-Developer and 1024-byte bulk packets achieved about 41.8 kB/s with zero
ACK timeouts, stale-request replies or fallbacks in the supplied progress sample.
This makes a general Master throughput limit less likely, not impossible.

The S3 log measured flash writes below 63 ms, local ACK enqueue below 1.5 ms and
transport polling gaps below 66 ms. These application measurements do not locate
delay or loss inside the WiFi driver/radio/network. The S3 also uses an older
Arduino/IDF high-performance SDK than the small panels.

The RGB bounce path continues reading PSRAM and servicing DMA interrupts even
when LVGL is idle. Espressif documents RGB/PSRAM contention with WiFi and flash,
and temporary PCLK reduction as a diagnostic/mitigation for some interactions.
This does not prove it causes this project's OTA failure:
[Espressif LCD FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html).

## Commands

- `wifidiag`: read `esp_wifi_get_ps()` and print the actual power-save mode,
  Arduino's requested mode and RSSI. `actual PS=0 (NONE)` is the intended state.
  A getter failure prints its error rather than treating it as mode NONE.
  This command does not change WiFi settings.
- `rgbprobe`: query the selected test and last accepted pixel-clock request.
- `rgbprobe 15`: opt in to a 15-MHz pixel clock **only while local firmware OTA
  is active**. Outside that interval, the configured normal clock remains in use.
- `rgbprobe 14` / `rgbprobe 13`: select 14 or 13 MHz for the same OTA-only
  comparison. The selection can change during one transfer without rebooting.
- `rgbprobe normal`: disable the probe and restore the configured normal clock,
  including during an active update.

The S3 normal clock remains 16 MHz. The test defaults to disabled on every boot
and is never written to NVS. No automatic low-clock mode is enabled by installing
this firmware. Panel changes execute only on the GUI owner, not the CLI or
transport task. The SDK applies successful requests at VSYNC. The reported scan
rate is theoretical and follows the last accepted request, not a hardware clock
measurement. Errors are logged and retried at most once per second for the same
requested clock; a failed request does not publish an accepted clock.
Only `15`, `14`, `13` and `normal` are accepted arguments. Other values and the former
`low` alias are rejected without changing the selection. The probe cannot
select a test clock equal to or higher than the configured normal PCLK.

## Hardware Procedure

1. Install 1.4.49RC through the working USB/RS485 route and reboot. That initial
   installation still executes the old firmware's OTA code.
2. Keep the Master version, AP, image and 1024-byte packet setting fixed. Run
   `wifidiag` on the S3 serial console and capture its output.
3. Start a subsequent WLAN update with the probe disabled and capture a baseline
   progress line. During that same transfer, send `rgbprobe 15` to the S3.
4. Wait for `PCLK request=15000000 Hz, error=0x0`, then record sustained speed and
   ACK/retry diagnostics over 30-60 seconds. Confirm that the normal update page
   remains visible; a panel color-cycle is not an acceptable operating mode.
5. If further comparison is needed and the image is stable, try `rgbprobe 14`,
   then optionally `rgbprobe 13`. If the panel
   loses the image or starts cycling colors, immediately use `rgbprobe normal`.
   If restoring the clock does not recover the image, abort the update through
   the Master and restart the display. These are unverified hardware settings.
6. Send `rgbprobe normal` and compare again after the 16-MHz restore request.
   Do not start a display benchmark during this comparison.

Master timeout/retry counters and maximum timings are cumulative for the update;
they will not fall when a probe improves the current path. Compare changes in
those counters, the current ACK time and the sustained progress rate, rather
than expecting the historical maximum ACK time to reset after a clock change.

On abort, timeout or completion, local OTA becomes inactive and the GUI requests
the normal clock even if a reduced clock remains selected for a future update.
It retries if that request fails. Rebooting also restores the normal startup
configuration and disables the probe.

WiFi buffers, flash writer, packet authentication, retry policy, draw-buffer
allocation, GUI task settings, panel porches and graphic assets are not changed
by this probe. The existing 1.4.46 buffer test profile is deliberately retained
for this comparison, not promoted as an improvement.

## Interpretation

A repeatable speed change following 16 -> 6 -> 16 MHz would implicate the RGB
scanout interaction, but would not by itself distinguish CPU/PSRAM contention
from physical electrical/radio interference. No change would direct the next
investigation toward the S3 SDK/driver and network path. An actual power-save
mode other than NONE is a separate concrete discrepancy to investigate before
assuming the cached Arduino request is the driver's state. No confirmed OTA fix
is claimed by this diagnostic build.

## Historical 6-MHz Result: 1.4.47RC

The supplied device log confirms actual WiFi power save NONE both before and
after the 6-MHz request; the cached Arduino request was also NONE. The setter
accepted 6000000 Hz, and the theoretical scan report changed from 39.0 to 14.6 Hz.
After the transition, the bulk receive count rose from 137 to 1641 without any
additional cached duplicates (the counter stayed at 2). Flash writes stayed near
58 ms, with a maximum below 63 ms. The user reported good throughput but did not
provide a numerical Master progress-rate sample or a reverse-clock comparison.

The panel itself started cycling red/green/blue instead of showing the update
page. The user reports that the image returned after restarting the display.
This implicates a scanout-dependent interaction but does not distinguish CPU,
memory bandwidth, electrical interference or panel timing effects. The device
log does not demonstrate a complete OTA transaction or restoration without a
restart. The 6-MHz option is therefore removed in 1.4.48RC, not enabled by default
or promoted as a production fix. Neither 12 nor 10 MHz is hardware-validated yet.

## Historical 12-MHz Result: 1.4.48RC

The next supplied log records a 12000000-Hz request and a later return to
16000000 Hz, both accepted by the driver. The user reports about 30 kB/s with
the image absent at 12 MHz and about 14 kB/s with the image restored at 16 MHz.
These are user-reported progress rates, not normalized full-transfer averages.
The log confirms that receive increments rise in the reduced-clock interval;
duplicates remain zero there and later rise to 13 after returning to 16 MHz.
Flash-write maxima remain below 64 ms, with no reported write, authentication or
local ACK-enqueue failures. Actual WiFi power save remains NONE across the switch.

This is evidence of a reversible scanout-dependent throughput change, not proof
of a particular CPU/memory/electrical mechanism or the panel's timing limits.
The IDF 5.4.1 `esp_lcd_rgb_panel_set_pclk()` implementation changes the PCLK
request at VSYNC; it does not enable a color-bar mode:
[Espressif driver source](https://github.com/espressif/esp-idf/blob/v5.4.1/components/esp_lcd/rgb/esp_lcd_panel_rgb.c).

Do not use a missing image as a production solution. The 12-MHz option is removed
in 1.4.49RC; the untested lower 10-MHz option is also removed. The new 15/14/13-MHz
range brackets the interval between the observed usable normal clock and the
observed image-loss setting. None of those new settings is hardware-validated
yet. Automatic clock reduction remains disabled, and all non-clock settings
remain unchanged for this comparison.
