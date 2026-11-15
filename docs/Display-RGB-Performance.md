# RGB Display Performance

Display 800x480 **1.4.19RC** preserves the working JC8048W550 configuration:
16 MHz PCLK, 12-line internal bounce buffers, one PSRAM framebuffer, 180-degree
rotation, anti-aliasing and the existing internal-RAM reserve. It does not
require SDK changes. Other display firmware and the Master are unchanged.

## Measured Baseline

The supplied 1.4.18RC hardware log reported:

- 8.8 LVGL FPS, 39.0 Hz theoretical panel scan rate.
- 28 internal draw-buffer lines, 11.1 tiles per frame, 2.06 MPix/s.
- Handler average/maximum: 61.6/68.7 ms per call.
- Copy average/maximum: 1.4/2.5 ms per tile, not per complete frame.
- WiFi OTA: approximately 7-22 kB/s, versus 33 kB/s on a small display.

These rates describe different operations. The RGB scan rate does not imply
39 rendered UI frames per second. At the measured tile count, copies account
for roughly 15.5 ms per frame; eliminating copy work alone cannot make this
workload render at the panel's scan rate.

## Changes

The direct RGB565 copy path reverses aligned pairs with 32-bit loads/stores.
It keeps the original scalar path for incompatible alignments and trailing
pixels. Odd widths, odd coordinates, both source alignments, inversion and
edge tiles are covered by pixel-comparison and guard tests. The source buffer
is not modified, and no extra memory is allocated. The helper is forced inline
so its hot instructions remain in the caller's IRAM section.

Local OTA progress drawing is coalesced to two updates per second instead of
five. Forced/final updates and target changes are immediate. Updates of other
modules retain five updates per second. Firmware bytes, authentication, ACKs,
retries, flash staging and transfer limits are unchanged. This reduces UI work
during flashing; it is not proof that WiFi reception or flash writes are the
sole cause of slow OTA.

After benchmark completion or cancellation, all performance counters now start
a fresh window. Previously the accumulated 12-second counts survived with a new
start timestamp, producing misleading lines such as 104.4 FPS after an 8.8 FPS
benchmark. The displayed benchmark result is preserved before counters reset.

## Verification

Run `tools/tests/display_hybrid_native/run_rgb_performance.ps1` for actual
production helper/OTA tests. Run `tools/tests/check_display_board_syntax.py`,
then `tools/tests/check_rgb_copy_codegen.py` for ESP32 compiler and instruction
checks. These checks do not measure real PSRAM bandwidth or flash hardware.

After flashing, repeat the same benchmark and the same WiFi update on the same
access point. Compare FPS, handler/copy times, update kB/s, retries, free heap and
display stability. No new hardware speed result is claimed until that test.

## Temporary Render Diagnostics (1.4.20RC-Diag)

This was an instrumentation build, not another speed optimization. The large
display's `build_opt.h` enabled `-DOFE_RGB_RENDER_DIAGNOSTICS=1` for this test.
The firmware identifies itself as **1.4.20RC-Diag**. The normal configuration
defaults to zero when that flag is absent, and then compiles out the deep hooks.
No installed LVGL files or SDK settings are edited.

1. Compile and flash `Module/DisplayModule_800x480/DisplayModule_800x480.ino`.
   The changed configuration also requires Arduino to rebuild the LVGL units;
   reusing an older BIN will not contain the diagnostic hooks.
2. Check the startup log for `LVGL render diagnostics: ARMED`.
3. With developer mode enabled on the Master, open **System > Display benchmark**
   and press **Start**. Let the normal 12-second test finish.
4. Capture the `DISPLAY BENCHMARK` line and all following `LVGL DIAG` lines.

The report includes inclusive milliseconds per rendered frame for refresh,
layout, render traversal, drawing tasks, text, blending, object redraw and the
existing copy path. It also includes calls per frame, input time, hook coverage
and hook errors. "Errors" here means unmatched profiler begin/end hooks,
not OFE bus errors. Missing hooks produce `hooks=MISSING` and an invalid-capture
message instead of misleading zero-cost results.

Categories overlap: for example, text drawing can include blending. Do not add
their times. These are elapsed times and include interrupt/preemption delays,
not isolated CPU execution times. Instrumentation itself adds overhead, so this build's FPS must not
be presented as a production performance improvement or regression.

Time sampling starts outside LVGL callbacks, on the next benchmark loop pass.
Normal use and OTA leave it disabled. Leaving the benchmark, opening connection
setup, starting an update or losing developer mode cancels sampling. Completion
prints one report and disables sampling again. There is no trace buffer or extra
task: only a small fixed counter block is used. Compiled-in diagnostic checks still
have a small cost even when disabled; remove the build flag for normal releases.

RGB timings, DMA/framebuffer topology, memory reserve, 32-bit copy path and OTA
transfer/ACK behavior are unchanged from 1.4.19RC. Its local OTA progress drawing
remains limited to two refreshes per second. The subsequent hardware report
measured about 31 kB/s over WiFi; that observation is not a guarantee for other
networks.

Run `tools/tests/display_hybrid_native/run_render_diagnostics.ps1` for profiler
lifecycle, cancellation, nested scopes, inactive clock use and rollover tests.
After the board compiler check, `tools/tests/check_render_diagnostics_build.py`
compiles five actual installed LVGL C units, checks their profiler linkage and
literal-tag folding, and verifies that the normal production mode has no hooks.

## Original Glyph Cache Experiment (1.4.21RC)

The valid hardware capture from 1.4.20RC-Diag measured 8.3 FPS, 67.06 ms/frame
refresh time, 0.23 ms layout, 10.55 ms text and 15.66 ms copying. Object redraw
was 45.85 ms, but includes child traversal and drawing; it is not an independent
45.85 ms layout cost. Hook coverage was complete and errors were zero.

1.4.21RC removes the temporary diagnostics build flag. The only new rendering
optimization is a cache for the existing 14-pixel Montserrat Medium font used by
most labels. LVGL's own decoder expands its original 4-bit alpha samples once
into static A8 glyphs in PSRAM. The software renderer can then blend these
directly, without decoding them again for each tile. This is not a different
font or reduced anti-aliasing: all character maps, advances, offsets, line and
underline metrics, kerning and symbol fallbacks are preserved. Larger fonts,
widget geometry, styles, animations, touch, panel timing, copy/rotation and OTA
are unchanged.

The cache uses one 17,608-byte PSRAM allocation and a small fixed descriptor
in internal RAM; it does not consume another draw buffer or lower the SRAM
reserve. Initialization occurs once, before widgets use the font, and does not
allocate during rendering. Allocation failure, unsupported fonts or an older
LVGL backend retain the original font. The static-A8 fast path was enabled only
for the locally verified LVGL 9.6 interface. It is disabled by default as of
1.4.22RC; `-DOFE_RGB_FONT_CACHE=1` is an explicit experimental opt-in, not a
recommended production setting.

`tools/tests/display_hybrid_native/run_rgb_font_cache.ps1` links the actual
installed LVGL font decoder, font lookup, draw-buffer and original font sources.
It checks all Latin-1 character pairs plus bullets, German characters, blank
glyphs, symbol fallbacks and missing characters: 65,286 comparisons of pixels
and metrics. It also checks raw static A8 output, guard bytes, PSRAM-only
allocation, allocation failure, idempotence, bounded map rejection and a
cache-disabled build. The installed LVGL/SDK are not modified. The native test
does not exercise panel DMA, WiFi traffic or real PSRAM/cache bandwidth.

For an explicitly enabled experimental build, check for `LVGL original font cache: 17608
bytes in PSRAM`, then repeat the same 12-second benchmark and real scrolling.
Compare the normal build with the previous normal result of about 8.9 FPS, not
with diagnostic FPS. This optimization targets only repeated glyph decoding;
no hardware FPS gain is claimed until measured, and it does not eliminate all
object traversal or drawing work.

## Home Refresh Regression Rollback (1.4.22RC)

The 1.4.21RC hardware benchmark measured 9.0 FPS versus about 8.9 FPS for the
previous normal build: no clear speed improvement. Handler time was 60.1/65.8 ms,
with ordinary scrolling still showing spikes up to 171.5 ms. The user also
reported slower Home updates. The benchmark does not prove that the font cache
caused the Home slowdown, but it provides no reason to keep that extra PSRAM
traffic enabled.

1.4.22RC therefore restores the exact original font pointer and decoder in the
standard build. The experimental cache object is not instantiated or initialized,
and its 17,608-byte PSRAM allocation does not occur. The startup marker is
`LVGL font cache: OFF (original font path)`. Deep render diagnostics remain off.
Home coalescing remains at 100 ms; no new refresh delay, layout, style, panel,
DMA or OTA changes are combined with this rollback. Native tests cover the
explicitly enabled, explicitly disabled and unconfigured/default modes.

Recheck visible Home changes (power, Work, afterrun and RPM) on the panel. Their
effective update rate also depends on incoming Master data and rendering time;
100 ms is a coalescing limit, not a guarantee of 10 visible updates per second.
Hardware confirmation is still required before calling the regression resolved.

The user subsequently confirmed that Home update speed was back to normal.
The next investigation leaves this baseline unchanged and uses a separate
reference sketch: see [RGB Reference Benchmark and SRAM Audit](Display-RGB-Reference.md).

## Tested SRAM Profile Becomes Standard (1.4.23RC)

The successful 1.4.22RC-SramTrial memory profile is now enabled in the normal
large S3 sketch's bundled `build_opt.h`. LVGL's 32 KiB base pool joins the
existing five PSRAM pools, preserving 192 KiB total widget capacity and the
same allocator. This frees internal SRAM for the existing guarded draw-tile
allocation; it does not guarantee a fixed post-boot heap or tile size.

The user confirmed slightly smoother scrolling, no artifacts, fast Home
updates, working Bluetooth and about 30 kB/s WiFi OTA. A follow-up snapshot
showed 2h 57m uptime and about 82 KiB free heap with no offline events. This
supports promotion but does not replace continued long-run testing.

Fonts, layout, antialiasing, Home cadence, PCLK, porches, bounce buffers, rotation,
copy path, OTA logic and runtime reserves remain unchanged. Font caching and
deep render diagnostics remain off. The standard version is **1.4.23RC**,
without the trial suffix. See [SRAM profile details](Display-RGB-SRAM-Trial.md).
