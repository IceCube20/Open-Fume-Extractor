from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
NAMES = ("DisplayModule_320x480", "DisplayModule_ST7796_320x480")


class SmallDisplayProfiles(unittest.TestCase):
    def test_identical_helpers(self):
        for file in ("OfeSmallDisplayProfile.h", "OfeSmallDisplayProfileUi.inc.h"):
            copies = [(ROOT / "Module" / name / "src" / file).read_bytes() for name in NAMES]
            self.assertEqual(copies[0], copies[1])

    def test_gui_owns_switch_not_cli(self):
        for name in NAMES:
            source = (ROOT / "Module" / name / (name + ".ino")).read_text(encoding="utf-8-sig")
            service = source.split("static void process_display_ui_once() {", 1)[1].split("void setup()", 1)[0]
            loop = source.split("void loop() {", 1)[1]
            self.assertIn("small_profile_tick();", service)
            self.assertNotIn("small_profile_tick();", loop)
            self.assertIn("if (small_profile_command(command)) return;", source)
            self.assertIn("allocateBootPsram(screen_w, selected_lines)", source)
            self.assertIn("small_display_profiles.publish(selected_lines);", source)
            helper = (ROOT / "Module" / name / "src/OfeSmallDisplayProfileUi.inc.h").read_text()
            command, tick = helper.split("static void small_profile_tick()", 1)
            self.assertNotIn("lv_display_set_buffers", command)
            self.assertLess(tick.index("allocatePsram"), tick.index("lv_display_set_buffers"))
            self.assertLess(tick.index("lv_display_set_buffers"), tick.index("heap_caps_free(previous)"))
            for guard in ("fw_update_active", "status.update_active", "display_benchmark.active",
                          "display_ble.active()", "screensaver_active"):
                self.assertLess(tick.index(guard), tick.index("small_display_profiles.take()"))
            self.assertNotIn("prefs.", helper)
            self.assertNotIn("delay(", helper)

    def test_timing_and_flush_paths_retained(self):
        qspi = (ROOT / "Module" / NAMES[0] / (NAMES[0] + ".ino")).read_text(encoding="utf-8-sig")
        spi = (ROOT / "Module" / NAMES[1] / (NAMES[1] + ".ino")).read_text(encoding="utf-8-sig")
        self.assertIn("#define DISPLAY_QSPI_FREQ 60000000", qspi)
        self.assertIn("#define DISPLAY_USE_PANEL_HW_ROTATION 0", qspi)
        self.assertIn("qspi_idf_send_full_frame()", qspi)
        self.assertIn("#define DISPLAY_SPI_FREQ 80000000", spi)
        self.assertIn("#define DISPLAY_DIRECT_PARTIAL_FLUSH 1", spi)
        self.assertIn("#define DISPLAY_USE_PANEL_HW_ROTATION 1", spi)
        for source in (qspi, spi):
            self.assertIn("#define DISPLAY_LVGL_HANDLER_INTERVAL_MS 5", source)
            self.assertIn("lvgl_disp, lvgl_draw_buf, NULL, lvgl_buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL", source)

    def test_benchmark_finish_and_cancel_reset_the_complete_window(self):
        for name in NAMES:
            source = (ROOT / "Module" / name / (name + ".ino")).read_text(encoding="utf-8-sig")
            finish = source.split("static void display_benchmark_finish(uint32_t now) {", 1)[1].split("\n}\n", 1)[0]
            cancel = source.split("static void display_benchmark_back_event(lv_event_t*) {", 1)[1].split("\n}\n", 1)[0]
            reset = source.split("static void display_benchmark_reset_counters(uint32_t now) {", 1)[1].split("\n}\n", 1)[0]
            self.assertGreater(finish.index("display_benchmark_reset_counters(now);"), finish.index("debug_printf("))
            self.assertIn("display_benchmark_reset_counters(millis());", cancel)
            self.assertNotIn("perf_window_start_ms =", finish + cancel)
            for counter in ("perf_lvgl_frames", "perf_panel_frames", "perf_lvgl_tiles", "perf_lvgl_pixels",
                            "perf_handler_us", "perf_handler_calls", "perf_canvas_draw_us", "perf_panel_flush_us"):
                self.assertIn(counter + " = 0;", reset)
            if name == "DisplayModule_ST7796_320x480":
                self.assertEqual(reset.count("perf_panel_bytes = 0;"), 1)


if __name__ == "__main__":
    unittest.main()
