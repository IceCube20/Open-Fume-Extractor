# Shared Display UI

The shared style covers **Home**, System, module lists and details, alarms,
WiFi/BLE pairing, boot/status, firmware updates and the benchmark page.
The screensaver retains its existing layout, with a fan indicator beside output power.

## Shared Components

`src/OfeDisplayUi.h` provides the same flat surfaces, light/dark palette,
14 px labels, 18 px values, 44 px controls, 6 px control corners and dropdown
popups on the independent display sketches. The basic spacing unit is 8 px,
with 12 px page margins. System content starts below the fixed header and
ends before the main navigation. No enclosing cards are added to sections.

System uses two columns on 800x480 and stacked sections on small displays.
Language/theme controls share a baseline. ST7796 retains its LCD color profile.
Benchmark visibility still follows developer mode; connection setup and all
backlight options retain their original callbacks and stored values.

JBC USB details keep their fixed category, port and page controls, twelve-entry
window. All numeric inputs now use the same compact keypad from
`OfeDisplayNumericUi.h`, including Home and JBC FAE settings. The dialog is
360 px wide on small displays and 400 px wide on large displays, with the
header visible and background controls blocked. Values are horizontally
centered in the dialog and on Home; symmetric vertical padding centers the
numeric text. In module details, fields and read-only values are left-aligned
with their captions. Existing
integer limits/clamping and JBC USB decimal validation/readback are retained.

Other module details use the same compact back-arrow button and aligned title.
Read-only telemetry is arranged in wrapping, two-column value fields; controls
remain below the measured value area. All configured profile entries remain
accessible by scrolling. Widgets are reused, including when values change.
JBC WORK and STAND use soldering-iron and parking symbols. On JBC USB these
icons are limited to station activity, not time counters with similar names.
The JBC USB loading view is prepared before navigation, with the legacy view
reserved for unsupported Masters or allocation failures.
Address, firmware, alias/model and connection state share one compact identity row.
Long names use ellipsis; a fixed area keeps the connection state visible.
Master details add their IP to the common system block, without duplicating
CPU, heap, uptime or maximum loop time.

Header icons share a 20 px target centered on the same line, with optical
alignment for the different Bluetooth, alarm and serial/WiFi font baselines.
The clock keeps its vertical center with or without the date. Alarm counts,
Eco and Bluetooth retain their own reserved slots, including on small panels.

`OfeDisplayControlsUi.inc.h` builds the non-USB module controls once for all
display variants. Fan/IO GPIO and Universal/Modbus controls use full-width
rows on small displays and two columns on 800x480. Captions, readback values,
switches/sliders and selectors have distinct areas; widget user data and
callbacks still identify the original entity. JBC FAE, Weller and display
settings use consistently aligned label/control rows across the available width.
Benchmark back navigation uses that arrow at its existing footer position.
Dialog close buttons remain recognizable as an X. Module rows and alarm entries
use flat surfaces; connection forms keep their live header visible.
Alarm entries retain severity-colored borders and a subtle glow. Home uses the
same navigation, fields and controls, while preserving its live-update cadence.
The large Home page fits its routing selectors above navigation. Small displays
retain scrolling, with a little more usable height below the fixed header.
On small displays, module cards always follow the extractor settings; their
position is derived from the bottom of those settings rather than a fixed offset.
The Fan/IO Home card measures its current title/status/detail text before the
cards are stacked, so long GPIO summaries stay inside their frame. If necessary,
the Home content scrolls instead of drawing into the next card or navigation.
On small displays, the OTA target/version has a bounded two-line area separate
from the writing status and progress bar.

The module list begins directly below the fixed header. Home uses the same
afterrun labels on both sizes. JBC activity has separate soldering-iron and parking
icons in the card header, with counts summed across fresh per-module port masks, not OR-combined
Master input bits. Incomplete multi-module data displays "--" until refreshed.
FAE has no separate Stand message: a connected station without Work counts as
Stand, matching the Master. USB station port masks retain their actual Stand bits.
Parking is amber when active; both SVG-derived masks have antialiased edges.

The supplied fan SVG uses a centered 21x21 mask with a fixed integer pivot.
Its speed follows displayed output power (6 seconds/revolution near 0 percent,
1.5 seconds/revolution at 100 percent); 0 percent and disabled outputs are still.
Speed changes preserve the rotation angle. Module accents match the Master:
JBC #39c779, Fan/IO #4a90d9, Weller #55b9ca, Universal #f1b84b, Modbus #e28b52.
Extraction and screensaver icons use their green status text colors.
Inactive icons are gray. Home animations pause when leaving Home.
The screensaver has its own fan animation, paused while the backlight is off;
it follows the existing minute-by-minute burn-in shift of the power label.
The icon is not an RPM sensor and does not replace actual fan feedback.
Vector sources and the reproducible A8 generator are in tools/assets and
tools/generate_display_activity_icons.cjs.

Universal RS232 and Modbus RTU have separate Home cards, including cached
profile values and navigation to the corresponding module details/list.
They no longer count as Fan/IO Home cards just because they expose an output.
On large displays, the right module column scrolls independently; extraction
controls on the left stay fixed. Small displays keep the shared vertical flow.
Alarm outlines use the same one-pixel border as other list entries, with a
subtle two-pixel severity glow.

The components do not own transport, settings persistence or telemetry refresh
cadence. Small LVGL icon animations do not change PSRAM allocation policy,
RGB/QSPI timing, touch, OTA or screensaver/wake policy. Styles are applied when creating controls, not on every
telemetry update. The P4 preparation and RGB SRAM snapshot use the same sources.

## Verification

`tools/tests/jbc_detail_ui/run.ps1` builds the installed LVGL with the real fonts,
the shared UI headers, extracted production page constructors and backlight
controls, and the actual WiFi/BLE form.
It renders both sizes/themes and the ST7796 variant, and checks control bounds,
alignment, numeric validation, cancellation, confirmation and settings callbacks.
The actual header constructors are also rendered with all indicators enabled;
glyph ink centers and single/two-line clock alignment are checked explicitly.
It also compiles and renders the actual shared module-control builder with
32 profile channels, checks widget bounds/entity IDs and tests the generic
integer keypad's confirm, clamping and cancellation paths. Numeric alignment
and symmetric padding are asserted on both resolutions and themes.
Render output goes into a separate temporary folder, not an Arduino build cache.

`tools/tests/test_display_ui_preserved.py` checks pre-migration fingerprints of
the screensaver, live-update functions and shared rendering helpers. Home
construction and navigation were deliberately brought into scope after the
initial migration; the original baseline is retained for the remaining paths.
Do not recapture it to conceal unrelated behavior changes.

Source regressions are in `tools/tests/test_display_ui_source.py` and
`tools/tests/test_display_settings_source.py`. Board compiler checks do not
replace hardware testing. Verify both themes/languages, all settings, dropdown
popups, scrolling, navigation, numeric editing and screensaver/wake on the panel.
