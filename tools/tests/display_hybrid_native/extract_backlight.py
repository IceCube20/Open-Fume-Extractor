"""Extract production brightness functions, without loading panel/LVGL drivers."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[3]
display = sys.argv[1]
source = (root / f"Module/DisplayModule_{display}/DisplayModule_{display}.ino").read_text(encoding="utf-8")


def function(name):
    match = re.search(r"^static [^\n]+\b" + name + r"\([^\n]*\) \{", source, re.M)
    if not match:
        raise RuntimeError(name)
    start = match.start()
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


parts = []
if display != "320x480":
    parts.append(function("backlight_duty_from_percent"))
else:
    parts.append("""static uint32_t backlight_duty_from_percent(uint8_t pct) {
      return (uint32_t)map(pct, 0, 100, 0, (1UL << BACKLIGHT_PWM_BITS) - 1UL);
    }""")
parts.extend(function(name) for name in ("apply_backlight", "set_brightness", "screensaver_enter"))
parts.append(function("screensaver_wake").split("  screensaver_return_screen = nullptr;")[0] + "}\n")
deferred = re.search(r"  if \(flags & UI_DEFER_DISPLAY_SETTINGS\) \{[^}]+\}", source)
if not deferred:
    raise RuntimeError("Deferred brightness application missing")
parts.append("static void process_display_settings() {\n"
             "  const uint32_t flags = deferred_flags; deferred_flags = 0;\n"
             + deferred[0] + "\n}\n")
parts.append(function("lvgl_touch_cb"))
Path(sys.argv[2]).write_text("\n\n".join(parts), encoding="utf-8")
