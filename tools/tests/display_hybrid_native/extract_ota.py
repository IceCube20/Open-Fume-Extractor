"""Generate a test include from the actual display handlers, not a model of them."""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parents[3]
resolution = sys.argv[1]
sketch = root / "Module" / f"DisplayModule_{resolution}" / f"DisplayModule_{resolution}.ino"
source = sketch.read_text(encoding="utf-8-sig")


def function(name, text=source, qualifier="static "):
    match = re.search(qualifier + r"(?:void|bool|String|const char\*) " + name + r"\([^;]*?\)\s*\{", text)
    if not match:
        raise ValueError(name)
    start = match.start()
    depth = 1
    pos = match.end()
    # These short OTA functions contain no braces in string literals/comments.
    while depth:
        depth += (text[pos] == "{") - (text[pos] == "}")
        pos += 1
    return text[start:pos]


names = ("fw_update_abort_local", "fw_update_touch", "fw_update_check_timeout",
         "fw_update_buffer_reset", "fw_update_buffer_flush", "fw_update_buffer_append",
         "handle_fw_begin", "handle_fw_chunk", "handle_fw_end")
out = pathlib.Path(sys.argv[2])
out.write_text("\n\n".join(function(name) for name in names), encoding="utf-8")
router = (root / "OpenFumeExtractorMaster/src/MasterDisplayWifi.cpp").read_text(encoding="utf-8-sig")
out.with_name("generated_master_route.inc.h").write_text(
    "\n\n".join(function("MasterDisplayWifi::" + name, router, "")
                 for name in ("beginFirmware", "route")), encoding="utf-8")

out.with_name("generated_display_jbc.inc.h").write_text(
    "\n\n".join(function(name) for name in
                 ("station_name", "update_jbc_station_list", "home_jbc_station_models", "screensaver_jbc_text")),
    encoding="utf-8")

# Execute the production availability accounting, stopping before unrelated
# extractor/routing cleanup. No duplicate implementation of the timeout policy.
scheduler = (root / "OpenFumeExtractorMaster/src/MasterScheduler.cpp").read_text(encoding="utf-8-sig")
start = scheduler.index("  ModuleRecord* rec = registry_.find(dst);", scheduler.index("if (physical) return false;"))
end = scheduler.index("    if (was_online && !rec->online) {", start)
out.with_name("generated_module_timeout.inc.h").write_text(
    "static void record_timeout(uint8_t dst, uint8_t cmd) {\n" + scheduler[start:end] + "  }\n}\n",
    encoding="utf-8")

if resolution == "800x480":
    out.with_name("generated_rgb_tiles.inc.h").write_text(
        function("rotate_rgb565_180_inplace", source.replace("static inline void IRAM_ATTR", "static void")) +
        "\n" + function("prepare_large_lvgl_tiles"), encoding="utf-8")
    font = (sketch.parent / "src/OfeSerialPortFont.h").read_text(encoding="utf-8-sig")
    out.with_name("generated_display_interaction.inc.h").write_text(
        function("get_touch_point") + "\n" + function("ui_should_hold_heavy_updates") + "\n" +
        function("center_icon", font, "inline "), encoding="utf-8")
