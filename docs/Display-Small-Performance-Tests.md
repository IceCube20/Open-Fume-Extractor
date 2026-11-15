# Small Display Buffer Comparison

The AXS15231B QSPI and ST7796 SPI sketches now default to a 32-line RGB565
render buffer in PSRAM (B32), with B16 allocation fallback. This is a test
baseline, not a claim of a measured FPS improvement. The previous B16 profile
and the larger B48 profile are available without reflashing between runs.

The RGB 800x480 S3 and P4 sketches are not changed by this experiment.

## Run The Comparison

Compile and flash the relevant small-display sketch normally. In its serial
console, send one of these commands, then wait for `RENDER applied ...`:

```text
render b16
render b32
render b48
```

After each successful switch, open the existing System benchmark page and run
the same 12-second benchmark. The result already includes the actual buffer
size (`Tiles/B16`, `Tiles/B32`, or `Tiles/B48`). Keep radio state, master setup,
panel clock and UI settings unchanged. Three runs per profile help distinguish
a real change from variation between runs.

`render` alone reports active/pending buffer lines. `pending=B0` means there is
no pending request. `heap`, `tasks` and `perf` remain available. A profile change
resets the live performance-counter window so old/new buffer measurements are
not mixed. A completed benchmark result stays visible until the next run.

Compare FPS, tiles/frame, handler time, transfer time and CPU peak. For QSPI,
the full-frame transfer takes place after the LVGL handler. For ST7796, direct
SPI transfers are inside the handler, so do not add the SPI time to it again.

## Memory And Safety

At 480 logical pixels per row:

| Profile | PSRAM Buffer | Increase Over B16 |
| --- | ---: | ---: |
| B16 | 15,360 bytes | 0 |
| B32 | 30,720 bytes | 15,360 bytes |
| B48 | 46,080 bytes | 30,720 bytes |

Only the GUI owner changes LVGL buffers. The CLI publishes a bounded request;
it never changes LVGL pointers. Requests wait while an OTA update, benchmark,
Bluetooth pairing or screensaver is active, or until LVGL is ready. A second
request is rejected while one is pending.

The new buffer is allocated before replacing/freeing the old one. If allocation
fails, the current display remains intact and the console reports the retained
profile. Initial B32/B48 allocation failure falls back to B16, then the existing
startup allocation fallback remains available. Buffer swaps need temporary
PSRAM for both old and new buffers; they do not intentionally consume an
additional internal-SRAM render buffer.

Profiles are not written to NVS. Restarting restores the compiled default.
To select a different boot default, define `OFE_SMALL_DRAW_LINES` as 16, 32 or
48 in the sketch's build flags, or change its default in
`src/OfeSmallDisplayProfile.h`. Keep the two small-display helper copies in sync.
Custom builds with `DISPLAY_LVGL_DRAW_BUFFER_PREFER_PSRAM=0` retain their original
startup allocation policy; a serial profile command explicitly switches to the
requested PSRAM test buffer.

## Unchanged Paths

No fonts, colors, widgets, geometry, touch settings, refresh intervals or
backlight behavior were edited. The AXS15231B still uses the safe software
rotation/full-frame output at 60 MHz. ST7796 retains hardware rotation and
direct dirty-rectangle output at 80 MHz.

The original buffer-comparison phase did not introduce asynchronous flushes.
The subsequent ST7796-only DMA experiment is described below. QSPI remains
synchronous: the installed `Arduino_ESP32SPI` polls register blocks, and
`Arduino_ESP32SPIDMA` also waits inside its pixel methods. Supplying `buf2`
alone does not make those transports asynchronous.

## Verification

Native tests compile all three boot defaults and exercise allocation fallback,
strict command parsing, queued requests, OTA/benchmark/BLE/screensaver guards,
GUI-only pointer replacement, failure rollback and repeated switching without
leaks. Source regressions verify identical helper copies and untouched panel
timings/flush modes. The pre-existing UI fingerprint remains unchanged apart
from the separately verified GUI-task ownership predicate rename.

On-device FPS, heap during pairing/OTA and long-term behavior still need the
owner's measurements. Compiler/native tests are not panel validation.

## Owner Results: 2026-10-23

The owner subsequently measured these benchmark results with the same UI:

| Display | B16 | B32 | B48 |
| --- | ---: | ---: | ---: |
| AXS15231B QSPI | 13.4 FPS | 14.8-15.0 FPS | 14.7 FPS |
| ST7796 SPI | 16.4 FPS | 18.8-18.9 FPS | 17.8 FPS |

B32 remains the default: approximately 10-12% more QSPI FPS and 15% more SPI FPS
than B16 in these samples. B48 did not improve the result. These are a small
number of owner runs, not a statistical confidence bound or a stability test.

On QSPI, tiles/frame fell from 11.4 to 5.2 and average handler time from 48.7 ms
to 42.2-42.5 ms; the full-frame transfer remained about 18.5 ms. On ST7796,
average handler time fell from 54.3 ms to 47.2-47.6 ms, while SPI transfer time
remained about 23.3-23.4 ms per benchmark frame.

The logs also exposed a separate reporting bug: small-display benchmark finish
and Back/cancel reset only the periodic window start, not its counters. The
first subsequent `SMALL PERF` line could divide the 12-second benchmark counts
by a roughly 5-second window, inflating the apparent FPS. Both exits now reset
the full counter window, matching the RGB display's behavior. The reported
`DISPLAY BENCHMARK` values above were calculated before that transition and
remain valid. The periodic 32-46 FPS spikes immediately after those runs are
not additional scrolling benchmark results.

Native regressions exercise the production reset/report/cancel helpers and
verify a new five-second window with ten frames reports 2 FPS, not 38 FPS from
the previous benchmark's 180 frames. Source guards verify that benchmark finish
resets only after formatting/logging the final result. No rendering path was
changed by this reporting correction.

## ST7796 Asynchronous DMA Test (1.0.40RC)

**Hardware regression / recovery in 1.0.41RC:** the owner reported a black panel
with 1.0.40RC. The log showed active LVGL/DMA transfers and sufficient heap, but
those counters only confirm software/driver progress, not valid physical LCD
output. The root cause is not yet established. `DISPLAY_ST7796_ASYNC_DMA` now
defaults to `0`, restoring the original Arduino register-driven SPI backend.
Keep B32 and the dedicated GUI task; neither needs reverting. Recompile and
flash only the ST7796 sketch to recover. No NVS reset or master update is needed.

The following describes the retained, **opt-in and not hardware-validated**
experiment (`DISPLAY_ST7796_ASYNC_DMA=1`). Do not treat successful native or
compiler tests as proof that the panel displays an image. No installed Arduino
library is modified.

The adapter exclusively owns SPI2 through the ESP-IDF SPI master API at the
existing 80 MHz / mode 3 settings. Arduino_ST7796 still performs initialization,
rotation, color-profile setup and address-window programming. The new transport
does not change fonts, layout, colors, touch handling or backlight behavior.

LVGL has two PSRAM render buffers. Each completed tile is copied with RGB565
byte swapping into one bounded internal DMA staging buffer and queued without
waiting for transmission. LVGL can render the next tile into its other buffer.
Its GUI-owned flush-wait callback retrieves the completed transaction before
acknowledging the flush. The interrupt callback only records a timestamp, not
LVGL calls or allocations. The final tile is drained before leaving each GUI
handler/forced-refresh pass, before performance counters reset or profiles swap.
S3's SPI transaction-length register limits each transfer to 32 KiB. B48 tiles
are split across two persistent descriptors (queue depth two), retaining one
shared staging allocation. If only the first descriptor is accepted, completion
drains it and transmits only the remaining tail synchronously before releasing
the tile; pixels are not duplicated or skipped.

| Profile | Two PSRAM Render Buffers | Internal DMA Staging |
| --- | ---: | ---: |
| B16 | 30,720 bytes | 15,360 bytes |
| B32 | 61,440 bytes | 30,720 bytes |
| B48 | 92,160 bytes | 46,080 bytes |

The staging allocation requires at least 96 KiB of free internal SRAM remaining
at allocation time. This is an allocation guard, not a promise of minimum heap
during later BLE/Wi-Fi activity. The GUI stack and driver metadata also consume
internal memory. Growing a runtime profile temporarily needs both old and new
staging/render allocations; failure retains the current buffers/profile.
Shrinking reuses the already allocated staging buffer rather than fragmenting
internal heap. Thus after B48, a B16 run still reserves 46,080 bytes of staging;
restart with the desired compiled profile for clean memory comparisons.

If a second buffer or staging cannot be allocated at startup, the adapter uses
synchronous transfers. A bus/device initialization failure uses the original
Arduino transport only after cleaning up owned IDF resources. If SPI2 already
belongs to another driver, initialization fails safely instead of resetting
that driver's hardware. A queue failure sends that tile synchronously without
changing bus ownership. A DMA completion error is fatal rather than releasing
memory that hardware might still own. The driver is forbidden from silently
allocating an additional DMA copy for queued pixels.

The startup log must say `ST7796 transport: ASYNC DMA / two PSRAM draw buffers`
to confirm that overlap is active. `synchronous fallback` means it is not.
The serial `perf` command also prints the active path, staging capacity and
queue-error count, so it is not necessary to capture the boot log.
Pixel/byte/frame counters are committed on completion; SPI time is measured
to the interrupt timestamp, not extended by concurrent CPU rendering. It now
overlaps handler work, so do not add it to handler time. This new accounting
and backend make the SPI timing only approximately comparable with the earlier
register-polling baseline.

Native tests compile the actual adapter plus extracted production flush/profile
helpers. They exercise initialization ownership, queue failure, odd pixel counts,
unchanged source data, completion-only release/counters, timestamp wrap, bounded
memory, allocation rollback and 60 successive profile swaps. Target compiler
checks cover async B16/B32/B48 and the original transport branch. This is not
yet a hardware-validated FPS or stability improvement.

Run three B32 benchmarks against the previous 18.8-18.9 FPS baseline, then test
scrolling, live Home values, BLE pairing and both OTA transports. Check colors,
tearing/artifacts, heap/minimum heap and long-term operation before keeping this
test backend as the production default. No automatic clock increase is included.

## QSPI Full-Frame Overlap Test (1.4.30RC)

This experiment applies only to `DisplayModule_320x480` (AXS15231B), not ST7796,
RGB S3 or P4. It does **not** retry partial LCD updates or hardware rotation.
The panel still receives every byte of a native 320x480 RGB565 full frame,
with unchanged 60 MHz, address-window commands, 0x32/0x003C00 QIO prefix,
16-KiB chunking and continuation flags. Panel initialization and visible boot
frames use the previous synchronous path before starting the worker.

**Owner results / default restored in 1.4.31RC:** the overlap test measured
13.2 FPS with 51.8/68.4 ms handler time, 25.1/34.8 ms QSPI transfer time and
69% CPU peak. The earlier B32 synchronous results were 14.8-15.0 FPS,
42.2-42.5 ms average handler time, about 18.5 ms full-frame transfer and
49% CPU peak. This is a regression in these runs, not an improvement.
Additional PSRAM traffic/cache contention or worker scheduling is a plausible
explanation, not an isolated/proven cause. For a same-build comparison, send
`qspi sync` on 1.4.30RC and repeat the B32 benchmark under the same conditions.

`DISPLAY_QSPI_ASYNC_FULL_FRAME` now defaults to **0** in 1.4.31RC. This restores
synchronous full-frame scheduling without allocating the extra snapshot/task.
B32, the GUI task, 60 MHz, software rotation and appearance remain unchanged.
The experiment below is retained as opt-in (`DISPLAY_QSPI_ASYNC_FULL_FRAME=1`).
The existing
blocking queued-QIO function runs on a low-priority Core-0 task, below the
OFE/Wi-Fi task priorities. All LVGL/touch/UI work stays on the GUI owner. The
worker never calls LVGL or changes GUI state/performance counters.

The GUI keeps its existing canonical pre-swapped, software-rotated wire image.
Before starting a transmission it copies **all 307,200 bytes** to a second
64-byte-aligned DMA-capable PSRAM snapshot. That snapshot is immutable until
the worker's completion semaphore returns ownership. LVGL can then update the
canonical image during the full-frame transfer. Changes received while busy
are coalesced there, and the next submission takes the latest complete state.
Unchanged pixels remain correct without back-buffer damage synchronization.

The cost is 307,200 extra PSRAM bytes, a 4-KiB internal task stack, a semaphore
and task metadata. The start guard requires 96 KiB plus stack/metadata allowance
of free internal memory. Allocation/task failure keeps synchronous output and
cleans up partial allocations. The existing driver's temporary DMA copies and
queue depth are unchanged; the experiment does not claim to eliminate SRAM
use or guarantee a particular minimum heap under radio load.

The WLAN/Bluetooth settings panel, active BLE pairing and updates pause overlap
and drain the pending frame before returning to the synchronous full-frame
path. Opening link settings drains any earlier frame before a subsequent BLE
start click. Overlap resumes when these conditions end. Manual `qspibench`
holds the GUI diagnostic lock and drains the worker before direct panel calls.
No code changes to the radio, OTA or touch handlers are needed.

Frame counters and transfer timings are collected by the GUI only after
completion, including the final sparse update when no further pixels change.
Forced refresh waits for pending frames; benchmark start/finish/cancel reset
boundaries drain them. Reported QSPI time is worker transfer duration (including
scheduling/driver waits), and now overlaps GUI time; do not sum them as a serial
frame budget. The extra snapshot copy is reported separately by `perf`.

Queue failure drains all accepted descriptors, raises CS, releases the SPI bus
and reports failure to the GUI. The latest whole frame is then redrawn by the
existing synchronous Arduino_GFX fallback. A failure to retrieve DMA completion
is fatal rather than freeing descriptors/source memory still owned by hardware.

### Compare On Hardware

Compile/flash only the QSPI sketch. The startup log should say
`QSPI full-frame overlap: ASYNC` in an opt-in build; `synchronous fallback`
means allocation or the optional transport was unavailable. With the restored
default there is no overlap worker and no overlap startup line. Run the same B32 System benchmark
three times, compared with the previous 14.8-15.0 FPS baseline. Then test
scrolling, Home updates, BLE, both OTA transports, minimum heap and extended
operation. FPS gains are **not** hardware-confirmed by the compiler/native tests.

Serial commands, without another flash or NVS writes:

```text
perf
qspi sync
qspi async
```

`qspi sync` drains the worker before using the previous blocking full-frame
scheduling; `qspi async` re-enables overlap if the worker/transport are healthy.
Both reset the counter window and force a complete redraw, but leave the UI
and render profile unchanged. They are rejected during benchmarks, pairing or
updates. A transport error disables re-enabling async until restart. To omit
the worker and extra snapshot entirely, compile with
`DISPLAY_QSPI_ASYNC_FULL_FRAME=0`. A runtime switch to sync retains the allocated
snapshot/task for a later A/B switch.

Native regressions compile the production worker, actual queued-QIO subclass,
frame sender, flush collector and mode controls with a real parallel worker
thread. They hold DMA while render RAM changes, verify exact full-frame bytes,
full-size windows/QIO continuation, coalescing, sparse completion, force waits,
settings-panel pauses, allocation/task cleanup, first/mid-queue failure recovery,
timer wrap, sync/async controls and repeated frames without new allocations.
Target syntax checks cover both scheduling branches and object generation
checks the current default build; neither is a hardware test or firmware link.

The log's reset reason 11 corresponds to `ESP_RST_USB` in the installed SDK;
the prior `unknown` label merely omitted that enum value. 1.4.31RC prints
`USB reset` (and recognizes JTAG resets) rather than implying a new failure.
