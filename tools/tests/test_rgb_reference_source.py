"""Reference isolation/provenance guards, not a replacement for hardware tests."""
from pathlib import Path
import runpy
import unittest

ROOT = Path(__file__).resolve().parents[2]
REF = ROOT / "tools/benchmarks/DisplayRgbReference"
PROD = ROOT / "Module/DisplayModule_800x480"
PREPARE = runpy.run_path(str(ROOT / "tools/benchmarks/prepare_rgb_reference.py"))


class RgbReferenceSource(unittest.TestCase):
    def test_generated_files_match_current_production_and_upstream(self):
        outputs = PREPARE["generated_files"](Path.home() / "Documents/Arduino/libraries/lvgl")
        for name, data in outputs.items():
            with self.subTest(file=name):
                self.assertEqual((REF / name).read_bytes(), data)

    def test_production_stays_at_safe_baseline(self):
        source = (PROD / (PROD.name + ".ino")).read_text(encoding="utf-8-sig")
        self.assertIn("#define DISPLAY_RGB_BOUNCE_BUFFER_LINES 12", source)
        self.assertIn("#define DISPLAY_STATUS_UI_MIN_INTERVAL_MS 100", source)
        self.assertIn("#define DISPLAY_RGB_PCLK_HZ 16000000", source)
        memory = (PROD / "src/OfeDisplayMemory.h").read_text()
        self.assertIn("RUNTIME_RESERVE = 104U * 1024U", memory)
        self.assertNotIn("lv_demo_benchmark();", source)

    def test_reference_has_no_module_transport_or_config_writes(self):
        source = (REF / "DisplayRgbReference.ino").read_text()
        for token in ("Preferences", "putBytes", "OFE_FW_SIG", "Serial1", "Serial2", "Update.begin", "WiFi.begin("):
            self.assertNotIn(token, source)
        self.assertIn("WiFi.persistent(false)", source)
        self.assertIn("WiFi.setAutoReconnect(false)", source)
        self.assertIn("WiFi.disconnect(false, false)", source)
        self.assertNotIn("ESP.restart()", source)
        self.assertNotIn("RTC_NOINIT_ATTR", source)
        self.assertNotIn("afterReset", source)
        self.assertIn("no silent fallback", source)
        self.assertNotIn("Cache_WriteBack_Addr", source)

    def test_reference_selects_explicitly_before_allocating_or_running(self):
        source = (REF / "DisplayRgbReference.ino").read_text()
        setup = source[source.index("void setup() {"):source.index("void loop() {")]
        self.assertLess(setup.index("selection = reference_select_test();"), setup.index("panel.begin()"))
        self.assertLess(setup.index("selection = reference_select_test();"), setup.index("lv_demo_benchmark();"))
        menu = source[source.index("static ofe_reference::Selection reference_select_test() {"):
                      source.index("static void print_memory(")]
        self.assertIn("ofe_reference::command(static_cast<char>(Serial.read()), next)", menu)
        self.assertIn("for (;;) {", menu)
        self.assertIn("delay(5)", menu)
        self.assertIn("no automatic start", menu)
        self.assertEqual(menu.count("return "), 1)
        self.assertEqual(source.count("lv_demo_benchmark();"), 1)
        self.assertNotIn("Serial.read()", source[source.index("void loop() {"):])

    def test_reference_uses_upstream_suite_and_matching_copy(self):
        source = (REF / "DisplayRgbReference.ino").read_text()
        self.assertIn("lv_demo_benchmark();", source)
        self.assertIn("lv_demo_benchmark_set_end_cb(reference_finished)", source)
        self.assertIn("ofe_rgb_tile::copy180Direct", source)
        self.assertIn("LV_DISPLAY_RENDER_MODE_PARTIAL", source)
        self.assertIn("lv_display_flush_ready(display)", source)
        config = (REF / "ofe_reference_lv_conf.h").read_text()
        self.assertIn("#define LV_MEM_SIZE (48U * 1024U)", config)
        self.assertIn("#define LV_USE_PERF_MONITOR 1", config)


if __name__ == "__main__":
    unittest.main()
