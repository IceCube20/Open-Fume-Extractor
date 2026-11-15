# Display GUI Task

The maintained 320x480 QSPI, 800x480 S3 RGB, ST7796 and generated P4 sketches now
use the GUI ownership model tested by the owner on the P4 V3 build. The working
Downloads test sketch was not replaced or modified during this integration.

## Execution Model

- `lvgl_gui`: core 1, priority 2, 16 KiB internal stack, one blocked RTOS tick
  between service passes. ESP-IDF stack sizes and high-water marks are bytes.
- The entire former UI block runs here: deferred data-to-widget updates, touch
  handling via LVGL, screensaver/backlight, BLE UI, benchmark and panel flushing.
- RS485 and authenticated WiFi frame processing remain in the existing bus
  task. The shared UI guard defers updates from any non-GUI task, not just RS485.
- The Arduino loop retains physical status LEDs, CLI, CPU/loop diagnostics and
  the original transport fallback. GUI busy times also feed the existing maximum
  loop-work diagnostic, rather than disappearing from that measurement.
- CLI status/heap/performance and manual QSPI tests take the same mutex as the
  GUI service. This includes `lv_mem_monitor()` and multiword performance counters.
  A busy GUI causes the diagnostic command to return after a bounded wait.

Task ownership is published before notifying the GUI task to start. The split
is enabled only after both LVGL initialization and bus-task creation succeeded.
If the mutex or task cannot be allocated, the original UI service continues in
the Arduino loop. Transport handlers never wait for the GUI mutex.

The common helper is `Module/DisplayModule_800x480/src/OfeDisplayGuiTask.h`,
with identical copies in the other display sketches. The P4 generator copies it
from the S3 RGB port. Do not edit generated P4 files independently.

## Unchanged Behavior

The S3 UI service blocks were moved verbatim, checked by SHA-256 against their
pre-extraction content. Fonts, layout, images, animations, touch settings, LVGL
handler intervals, Home update cadence, render buffers and S3 panel timings were
not changed. The owner subsequently tested 24 MHz on the P4 successfully.
The sketch fallback and generator/board template now agree on that setting;
the GUI-task change itself does not change panel timing.
RNG, BLE, PPA, firmware/OTA validation and partitions retain their existing code.

The extra task costs approximately 16 KiB of internal stack plus RTOS metadata.
It does not imply 41 FPS on an S3. Rendering, RGB scanout and QSPI/SPI transfer
bandwidth still impose their own limits. Idle `0 FPS` means nothing changed on
screen; it is not a failed GUI task.

## Configuration And Checks

Optional build definitions, identical on all four ports:

```cpp
#define DISPLAY_GUI_TASK_ENABLED 1
#define DISPLAY_GUI_TASK_CORE 1
#define DISPLAY_GUI_TASK_PRIORITY 2
#define DISPLAY_GUI_TASK_STACK (16 * 1024)
```

Set `DISPLAY_GUI_TASK_ENABLED=0` through the sketch build flags for an A/B test.
Keep panel clock, renderer and scene unchanged between tests.

At boot, look for `GUI task started: core=1 priority=2 stack=16384 bytes`.
The serial `tasks` command reports GUI status and stack reserve; `status`,
`heap` and `perf` remain available. Allocation failures explicitly report the
Arduino-loop fallback.

Verification for this integration:

- Current S3 QSPI, S3 RGB and ST7796 sources pass their ESP32 compiler checks with
  installed board/SDK flags and project headers.
- The generated P4 sketch also passes the P4 SDK/PPA compiler check.
- Native enabled/disabled-task tests cover missing transport/UI, allocation
  failures and cleanup, idempotent start, ownership publication, diagnostic lock
  success/timeout, owner non-recursion, blocked yielding and micros wraparound.
- Display and P4 source regressions verify single UI ownership, exact S3 service
  extraction, synchronized helpers and reproducible P4 generation.
- These checks are not a complete firmware link or an on-device stress test.
  No upload was performed.

After recompiling and flashing, check Home updates and scrolling, every module
detail page, keyboards/settings, screensaver/two-touch backlight wake, pairing,
RS485/WiFi reconnection and OTA. Record GUI stack reserve and internal heap during
these actions, not only on an idle Home screen.
