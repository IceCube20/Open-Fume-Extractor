# Fan/IO LED Outputs

MCU 2.1.47 adds virtual LED outputs for Fan/IO 1.1.78 and Fan/IO Pro
1.1.73. Rebuild both the MCU and the affected module before using them.
Existing firmware without `led_schema=1` keeps its original editor and commands.

## Configuration

The existing GPIO hardware editor configures one addressable strip per module:

- Disabled by default; the data GPIO comes from the existing pin list.
- Reserved GPIOs and collisions with inputs, outputs, fan or pressure sensor
  pins are rejected by both the editor and the module.
- Up to 128 pixels and 32 independently named logical channels.
- 800 kHz GRB/RGB or GRBW/RGBW; RGBW provides a separate white value.
- Each channel controls a pixel range, color and solid/blink/breathe effect.
- Effects run locally at up to 25 updates per second, outside the bus state lock.
- Higher channel numbers win when active pixel ranges overlap.
- Outputs turn off after MCU communication loss, during module OTA, on reboot
  and after a successful hardware configuration change.

Use an appropriate external LED power supply and signal interface. The GPIO
is a data output, not a power source. The pixel limit is a software limit,
not a power-supply rating. Physical strip operation still needs hardware testing.

Configured channels appear in both the Boolean Designer and additional rules.
They are indicators, not candidates for the extractor's main output.
The existing limit of 32 distinct Boolean output targets remains unchanged.

## First Indicator: Step by Step

1. Update the MCU and the Fan/IO module to the versions listed above or newer.
   Back up the existing GPIO assignments before changing hardware configuration.
2. Open the module's GPIO hardware editor. Enable the LED strip, choose a free
   data GPIO from its existing pin list and select the actual strip's color order
   and RGB/RGBW model. Do not reuse a reserved or already assigned pin.
3. For an eight-pixel strip, set the physical pixel count to 8. Start with low
   brightness and a suitable external supply; connect grounds as required by the
   hardware. Never power a strip through the data GPIO.
4. Enable channel 1, give it a short meaningful alias, choose **First LED = 1**
   and a range of 4 pixels, then choose its color and solid/blink/breathe effect.
   The editor numbers pixels from 1; the protocol stores zero-based offsets.
5. Commit the hardware configuration. The successful commit turns active LED
   outputs off; it does not automatically create a rule to turn them back on.
6. Add a routing rule or Boolean definition with that named LED channel as its
   output. For example, use an explicit system activity signal as the input.
   Save and enable the rule, then verify both the on and off states.
7. Add more channels for other non-overlapping ranges as needed. If active ranges
   overlap, the higher channel number takes priority, not the last edited one.

An enabled channel defines how its pixels look **when its logical output is on**.
It is not permanently on just because its configuration checkbox is enabled.
Changing the color/effect is a hardware configuration operation; routing controls
its Boolean on/off state. These indicators do not replace safety-rated signalling.

## Protocol / Storage

`CMD_IO_CONFIG` uses the existing begin/commit/abort transaction:

- Action 8: `action, pin, model, brightness, countLE16`.
- Action 9: `action, channel, enabled, firstLE16, countLE16, r, g, b, w,
  effect, periodLE16, name[0..18]`.
- Entity IDs 100..131 are Boolean channel states and controls.
- Bulk state reads use entity 99: eight bytes containing a little-endian
  state mask and enabled mask. The MCU expands these to the individual states,
  avoiding 32 separate bus queries. Non-Fan/IO modules keep their original IDs.

Hardware configuration version 4 migrates versions 1..3 with LEDs disabled.
Existing GPIO assignments and aliases are preserved. Old firmware cannot read
the new version-4 hardware configuration; keep a configuration record before
downgrading.

## Web UI

The theme selector provides Auto, Light and Dark, stored per browser.
Wire selection supports Ctrl/Shift-click on desktop and the selection tool
with larger wire hit areas on touch screens. Selected wires can be deleted or
their manual routes unlocked together.

## Verification

- `python tools/tests/check_fanio_led_native.py`
- `node tools/tests/test_led_theme_browser.cjs`
- `node tools/tests/test_logic_selection_browser.cjs`
- Arduino CLI compile for both Fan/IO modules.
