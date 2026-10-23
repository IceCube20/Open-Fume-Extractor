"""Compile the production flush and profile replacement, not test reimplementations."""
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[3]
source = (root / "Module/DisplayModule_ST7796_320x480/DisplayModule_ST7796_320x480.ino").read_text(encoding="utf-8-sig")
output = []
for signature in ("static void st7796_complete_flush(lv_display_t*)",
                  "static void lvgl_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map)",
                  "static bool st7796_replace_profile(uint8_t* candidate, uint32_t bytes)"):
    start = source.index(signature + " {")
    end = source.index("\n}\n", start) + 3
    output.append(source[start:end])
Path(sys.argv[1]).write_text("\n".join(output), encoding="utf-8")
