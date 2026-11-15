# OFE RGB SRAM Profile: Promoted to Standard

The tested SRAM profile is the **standard large S3 display build from 1.4.23RC**.
Use `Module/DisplayModule_800x480/DisplayModule_800x480.ino` for normal builds.
The separate trial folder is retained as a labelled, generated snapshot, not
an independently maintained firmware. Small displays, P4 memory configuration,
Master, installed SDK and LVGL libraries are unchanged by this promotion.

The user reported smoother scrolling without artifacts, fast Home updates,
WiFi OTA around 30 kB/s and working Bluetooth. The supplied follow-up status
showed 2h 57m uptime, about 82 KiB heap, 3% CPU at that moment, loop max 11 ms,
and no offline events. These are observations, not an unlimited stability or
constant-FPS guarantee.

## Why This Test Exists

The supplied official LVGL reference results improved from 11 to 14 FPS in the
scrolling scene when the internal RGB565 draw tile increased from 28 to 48 lines.
The tile costs 32,000 extra internal bytes. The real application cannot simply
borrow those bytes from the protected radio/runtime headroom.

The former 1.4.22RC standard ELF showed a 32,768-byte internal `work_mem_int` LVGL base
pool. The detail records, parse scratch, universal cache and OTA write buffer
are already in PSRAM. Moving those again cannot recover another 32 KB. Most
other large internal symbols belong to the SDK, WiFi, BLE or interrupt stacks;
changing them is not a lossless application-data relocation.

The promoted profile moves **only LVGL's 32 KiB base allocator pool** to PSRAM. The other
five 32 KiB PSRAM pools, the TLSF allocator and total 192 KiB widget capacity are
unchanged. The configured internal draw-tile target is still 48 lines, selected
automatically using the existing allocation guards. LCD bounce buffers remain
internal, 12 lines each; framebuffer, clocks, rotation and copy path are unchanged.

The standard base-pool symbol disappears from the trial's compiled real LVGL
allocator unit. Net application heap gain can differ slightly due to pointer
storage, alignment and SDK allocation overhead. Neither 48 lines nor a minimum
post-boot free-heap value is guaranteed by a source change alone.

## Tradeoff and Safety

- Bundled S3 `build_opt.h` enables `OFE_RGB_WIDGET_POOL_PSRAM=1` for the sketch
  and LVGL library together. The configuration's no-flag fallback remains 0
  for controlled comparisons and the independent, untested P4 port.
- The normal build reports **1.4.23RC**. The generated snapshot adds only the
  `OFE_RGB_SRAM_TRIAL=1` version label and reports **1.4.23RC-SramTrial**;
  both use the same memory profile.
- The 104 KiB runtime / 112 KiB startup draw-allocation reserves are unchanged.
  These are allocation-time guards, not guarantees of free heap after all
  screens, workers, network peaks or BLE operations have started.
- The base pool is preallocated before `lv_init()`. If PSRAM allocation fails,
  UI initialization stops with a specific log message instead of handing LVGL
  a null pool or silently consuming internal SRAM.
- The hook returns exactly one boot-lifetime, 64-byte-aligned PSRAM pool. It
  does not repeatedly allocate while rendering or change the allocator itself.
- All visual assets, fonts, layout, antialiasing, Home cadence, transport and
  firmware-update logic are copied unchanged into the test snapshot.

LVGL object and allocator access will now use PSRAM more often. That can offset
the larger draw tile's benefit or reintroduce RGB contention. Espressif documents
the sensitivity of RGB bounce-buffer operation to external-memory bandwidth:
[RGB LCD guidance](https://docs.espressif.com/projects/esp-idf/en/v5.3.6/esp32s3/api-reference/peripherals/lcd/rgb_lcd.html).
Do not promote this profile based on free-heap numbers alone.

## Build and Verify

1. Open `Module/DisplayModule_800x480/DisplayModule_800x480.ino` in Arduino.
   Keep the bundled `build_opt.h`; no experimental sketch is needed anymore.
2. Select the same installed OFE 800x480 High Performance board/SDK and settings
   as the normal display. Compile/upload over USB without erasing NVS.
3. Check firmware `1.4.23RC` and these boot lines:
   `LVGL widget pools: 32768 bytes PSRAM + 5 x 32768 bytes PSRAM`
   and `LVGL draw buffer: ... lines`. A confirmed 48-line allocation is the
   intended comparison; do not assume it happened if the log selects fewer.
4. Connect to the same Master and run the normal OFE display benchmark. Record
   its result plus `LVGL PERF` lines while scrolling the same module list.
5. Check actual Home response, touch, text, artifacts, screensaver and WiFi
   OTA. Use the serial `heap` command after the workers have
   started and again after opening details, scanning WiFi and using BLE.
6. Keep monitoring longer runs and radio peaks. For a controlled return to the
   old internal base pool, change the existing build flag to
   `-DOFE_RGB_WIDGET_POOL_PSRAM=0` and rebuild; do not add contradictory flags.
   No pairing reset or NVS erase is required.

Use the normal sketch for future signed releases, not the labelled snapshot.
This change does not sign binaries, publish a release or update flashed devices.

## Reproduce and Check

The snapshot is generated, not separately maintained. It copies source files
only, never production build outputs or binaries. The only compilation change
is the extra version-label flag. The sketch filename is adapted for Arduino and the
shared sprout forwarding include is resolved to the unchanged original bitmap.
The manifest records file hashes. Regenerate after any production change:

```powershell
python tools/benchmarks/prepare_rgb_sram_trial.py
python tools/benchmarks/prepare_rgb_sram_trial.py --check
python -m unittest tools.tests.test_rgb_sram_trial_source
& tools/tests/display_hybrid_native/run_rgb_widget_pool.ps1
python tools/tests/check_rgb_widget_pool_build.py
```

Native tests cover default/disabled allocation-free behavior, PSRAM-only flags,
alignment, failure/retry, size validation and reuse. The target check compiles
the current complete sketch, backend and installed LVGL allocator in both modes,
partially links the internal-reference and promoted standard profiles, checks
the C/C++ hook and verifies the internal pool's presence/absence. It is not a
complete firmware link, upload or hardware test.
