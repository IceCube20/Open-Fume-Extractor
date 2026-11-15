# RGB Reference Benchmark and SRAM Audit

The application baseline remains **Display 800x480 1.4.22RC**. No production
firmware, SDK, installed LVGL source, Home update cadence or visual design is
changed by this reference test. The font cache remains disabled.

## What the Measurements Establish

The normal application benchmark measured approximately 8.9-9.0 FPS. The cache
experiment did not establish a meaningful improvement and slowed Home updates
in the user's test, so it was disabled again.

Espressif publishes an ESP32-S3 / RGB / 800x480 / RGB565 reference result of
25 benchmark FPS with `esp_lvgl_adapter` and LVGL 9.4. A different ESP-BSP test
lists 11 FPS for scrolling containers, 18 FPS for multiple labels and 7 FPS for
the widgets demo. Neither is the same scene, LVGL version, buffer topology or
radio workload as OFE. They are useful references, not promises for this panel.

- [Espressif platform results](https://docs.espressif.com/projects/esp-techpedia/en/latest/esp-friends/advanced-development/lcd-application-note/overview.html)
- [ESP-BSP scene results](https://github.com/espressif/esp-bsp/blob/master/bsp/esp32_s3_lcd_ev_board/README.md#lvgl-benchmark)
- [Espressif buffer/performance guidance](https://github.com/espressif/esp-bsp/blob/master/components/esp_lvgl_port/docs/performance.md#lvgl-buffer-configuration)

Espressif identifies buffer configuration as a major performance lever and
reports substantial losses below 10% screen-sized SRAM draw buffers in its
tests. OFE's measured 28-line tile is 5.83% of the screen. This is a testable
hypothesis, not proof that it explains all rendering time.

## SRAM Audit of the Supplied Hardware Log

| Item | Location | Bytes |
| --- | --- | ---: |
| RGB565 framebuffer, 800 x 480 | PSRAM | 768,000 |
| Two RGB bounce buffers, 12 lines each | Internal SRAM | 38,400 |
| LVGL draw tile, 28 lines | Internal SRAM | 44,800 |
| Built-in LVGL allocator pool | Internal SRAM | 32,768 |
| Status + last drawn status | Internal SRAM | 1,384 |
| Module summaries + expected modules | Internal SRAM | 2,176 |
| WiFi startup internal-heap delta | Internal SRAM | 42,624 |
| Selected detail + parse scratch | PSRAM | 16,320 |
| Universal module cache | PSRAM | 124,160 |
| OTA write buffer | PSRAM | 4,096 |

The WiFi delta is an observed difference, not a permanently fixed allocation.
This table is not an exhaustive heap map: task stacks, driver state, other
globals and transient allocations also exist. Do not subtract static pools
again from a reported free-heap value that already excludes them.

The log reports 107,564 free internal bytes after reserving the draw tile and
106,520 after LVGL initialization. The runtime draw allocation guard requires
104 KiB (106,496 bytes) of headroom. A 48-line tile is 76,800 bytes, or **32,000
bytes more** than the current tile. Simply making that change would reduce the
observed post-LVGL free space to roughly 74.5 KB before other runtime effects,
below the protected reserve. A 120-line (25%) SRAM tile alone would need
192,000 bytes. Extra PSRAM does not increase this internal-SRAM budget.

The current cap is therefore not evidence by itself of a fragmentation bug.
There is no justified production change to the reserve, bounce buffers, pixel
clock, PSRAM frequency or BLE/WiFi buffers in this step.

The installed high-performance SDK already enables
`CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=1` and dynamic WiFi TX buffers. It has eight
static RX buffers, 32 dynamic RX buffers, 32 dynamic TX buffers and a 4,096-byte
`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` threshold. These are read from its
`esp32s3/qio_opi/include/sdkconfig.h`, not newly applied settings. Simply enabling
WiFi/LwIP PSRAM support again is therefore not a new optimization. Driver DMA
requirements and small allocations still consume SRAM; reducing radio buffer
counts or changing the allocator threshold would be a separate system-wide
experiment, not a demonstrated lossless improvement. The SDK is unchanged.

## Separate Reference Sketch

Open:

`tools/benchmarks/DisplayRgbReference/DisplayRgbReference.ino`

This is **temporary USB test firmware**, not an OFE module. It has no pairing,
bus participation, extraction controls or OTA server/signature. The Master
will not receive display replies during this test. Use a spare display or an
appropriate maintenance window. Restore the normal 1.4.22RC display firmware
over USB afterward. Do not upload it through the Master or publish it as an
OFE release. It does not read or rewrite the OFE configuration namespaces.

Use the same installed **OFE 800x480 High Performance** board/SDK, CPU 240 MHz,
OPI PSRAM and USB serial settings as the normal display. This harness requires
the installed LVGL 9.6.x interface. It deliberately uses that installed version
rather than pretending to reproduce Espressif's older 9.4 binary exactly.

The sketch runs the unmodified installed `lv_demo_benchmark()` suite, including
its scrolling-container and widgets scenes. The RGB driver, timing/polarity
defaults and lossless 180-degree copy helper are generated from the production
source. It retains 16 MHz PCLK, 12-line bounce buffers, RGB565, one PSRAM
framebuffer, internal partial draw tiles, 180-degree rotation and a 16 ms
refresh period. No explicit PSRAM framebuffer writeback is added.

Reference-only differences:

- No OFE UI/cache tree, RS485 task, Master traffic, BLE, touch or OTA.
- Original LVGL benchmark graphics/fonts, not OFE graphics.
- A 48 KiB internal LVGL pool plus five 48 KiB PSRAM pools. The upstream widgets
  demo requires a compile-time pool size of at least 38 KiB; OFE uses 32 KiB.
- Sysmon/performance monitoring and benchmark/widget demos are enabled.
- Optional WiFi STA driver initialization with dynamic buffers and sleep off,
  but **no AP association or traffic**. This approximates driver memory cost,
  not the production WLAN workload. No SSID/password is printed or requested.
- A 32 KiB reference-only allocation floor, not the production 104 KiB reserve.
  More SRAM is available because the rest of the OFE application is absent.
- Backlight fixed at 100% without changing the stored brightness preference.
  Use a stable supply and note this power-load difference when assessing artifacts.

These differences mean the reference cannot establish end-to-end application
performance or justify reducing the application's safety reserve. Compare
28 versus 48 **within this harness** first.

## Running the Comparison

1. Compile/upload the reference sketch over USB. Open the serial monitor at
   115200 baud. No flash erase or configuration reset is required.
2. At `REF SELECT`, send `1` for **28 SRAM lines with the WiFi STA driver idle**.
   There is no automatic start or timeout: the test waits for a valid selection.
   Leave it untouched until `REF DONE` appears, approximately 70 seconds later.
3. Press the board's hardware **RESET/EN** button. Reopen the serial monitor if
   USB reconnects. At `REF SELECT`, send `2` for **48 lines with the same idle
   WiFi driver**. Verify `REF CONFIG: lines=48 bytes=76800` before comparing.
   No second compilation is necessary.
4. Capture the `REF CONFIG`, `REF MEM`, `REF RESULT`, all `REF SCENE`,
   `REF OFFICIAL AVG` and `REF FLUSH` lines from both runs. Note any artifacts.
5. After another hardware reset, optionally use `3` and `4` for 28/48 lines
   with the radio off. `1` selects 28 lines with the idle radio. Repeat a pair
   to check variability.

Reference **v2** always selects explicitly before initializing the panel or radio
and allocates exactly the requested tile. Invalid characters, including serial
line endings, do not start the test. The screen need not show benchmark output
until a selection is sent. No selection is stored in RTC RAM or NVS, and the
test never requests a software reboot. Commands cannot change an active test.
A new boot is required for each run because the upstream benchmark maintains
per-run scene counters and timers. Allocation failure stops the test instead
of silently substituting a smaller tile.

In v1, the attempted software restart on this board resulted in
`RTCWDT_RTC_RST`, not a software-reset reason. The strict reset check then
selected 28 lines again. The two supplied v1 runs are therefore repetitions
of **28 lines**, not a 28/48 comparison. V2 removes that dependency rather than
accepting arbitrary watchdog resets as valid mode switches.

`REF SCENE` contains the upstream LVGL metrics. Its CPU/busy percentage is based
on LVGL's measurement, **not** the OFE dual-core CPU measurement. The raw suite
flush counters include scene transitions; they are not the official average FPS
and are not an actual VSYNC presentation count. The panel scan ceiling remains
approximately 39 Hz. A software FPS number does not establish tear-free output.

If 48 lines clearly improve the scrolling-container scene, SRAM draw-tile size
is a useful optimization target. If not, enlarging that tile in production is
not supported by this experiment. If the reference is faster than OFE, that
does not isolate one cause: UI workload, allocation and background traffic
still differ. Retest real Home changes, scrolling, radio traffic and artifacts
before accepting any subsequent production optimization.

## Hardware Results (28 and 48 Lines)

The two supplied v1 runs both reported 28 lines with the WiFi STA driver idle.
They produced the same rounded official average, **25.75 FPS across 16 scenes**.
The subsequent v2 run explicitly confirmed `lines=48 bytes=76800` with the same
idle WiFi mode. Selected scene results:

| Scene | 28 lines FPS | 48 lines FPS |
| --- | ---: | ---: |
| Single rectangle | 50.00 | 50.00 |
| Multiple labels | 20.86 | 20.25 |
| Screen sized text | 4.82 | 6.77 |
| Containers | 36.44 | 43.89 |
| Containers with overlay | 9.12 | 13.00 |
| Containers with scrolling | 11.00 | 14.00 |
| Widgets demo | 7.76 | 9.81 |
| Official suite average | 25.75 | 27.69 |

The scrolling scene reported approximately 60 ms render and 23 ms flush time.
Simple-scene software FPS above 39 does not increase the physical panel's scan
rate. The reference-only internal heap ended near 131 KB in both runs, without
an obvious end-of-suite free-heap decline between these boots. This is not a
long-duration leak test or evidence of production headroom.

At 48 lines, the scrolling scene's reported render time fell to 42.62 ms, while
flush time was 24.54 ms. The official suite average rose about 7.5%, scrolling
about 27.3%, and widgets about 26.4%. Not every scene improved. Internal free
heap at the end was 99,024 bytes, versus 131,016 in the first 28-line run.

The aggregate 27.69 FPS result does not predict that FPS for OFE's screen.
The scrolling scene is more relevant, but its content still differs from OFE's
roughly 9 FPS benchmark. These results support testing larger internal tiles,
not reducing production headroom or claiming artifact-free output. A separate
[complete OFE SRAM trial](Display-RGB-SRAM-Trial.md) tests relocating the base
LVGL widget pool while preserving the production allocation reserves.

## Preparation and Checks

The reference folder is already prepared. After changing the production driver,
LVGL configuration or installed LVGL version, regenerate its managed copies and
upstream wrappers:

```powershell
python tools/benchmarks/prepare_rgb_reference.py
python tools/benchmarks/prepare_rgb_reference.py --check
```

The generator writes only managed reference files. `reference-manifest.json`
records source hashes. Small C wrappers make the installed demo sources visible
to Arduino, which normally compiles LVGL's `src/` but not its `demos/` directory.
No third-party source is modified or downloaded.

Tests:

```powershell
python -m unittest tools.tests.test_rgb_reference_source
& tools/tests/display_hybrid_native/run_rgb_reference.ps1
python tools/tests/check_rgb_reference_build.py
```

The target check compiles the sketch, actual demo assets and complete installed
LVGL C core with the reference configuration, then performs a relocatable link
and checks for unresolved LVGL/demo symbols. It reuses only recorded board
flags from the display compiler check; it does not reuse incompatible LVGL
objects, modify the SDK, clear IDE caches, flash hardware or perform a final
firmware/partition link. The 28/48-line hardware results and successful v2
selection are recorded above; production-profile performance remains unverified.
