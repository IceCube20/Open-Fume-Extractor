"""Backlight settings must keep the display transport and older firmware working."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
NAMES = ("DisplayModule_320x480", "DisplayModule_800x480", "DisplayModule_ST7796_320x480")

class DisplayBacklightSourceTests(unittest.TestCase):
    def test_mqtt_dynamic_options_match_const_char_publisher_contract(self):
        source = (ROOT / "OpenFumeExtractorMaster/src/MasterMqtt.inc.h").read_text(encoding="utf-8")
        self.assertIn('const char* value_template, const char* extra_json = nullptr', source)
        self.assertIn('delay_options.c_str(), mqtt_module_command_leaf(addr, "display_backlight_timeout")', source)

    def test_policy_copies_and_all_three_display_integrations(self):
        expected = (ROOT / "Module" / NAMES[0] / "src/OfeDisplayBacklight.h").read_text(encoding="utf-8")
        for name in NAMES:
            directory = ROOT / "Module" / name
            self.assertEqual(expected, (directory / "src/OfeDisplayBacklight.h").read_text(encoding="utf-8"))
            sketch = (directory / (name + ".ino")).read_text(encoding="utf-8")
            self.assertIn("display_backlight.tick(millis(), module_eco_mode,", sketch)
            self.assertIn("updating || !have_drawn_status || OfeDisplayWifiUi::isOpen()", sketch)
            touch = sketch.split("static void lvgl_touch_cb(", 1)[1].split("static lv_obj_t* lv_label(", 1)[0]
            self.assertLess(touch.index("if (screensaver_wait_release)"), touch.index("if (screensaver_active)"))
            self.assertIn("if (light_was_off) {\n      apply_backlight();\n      screensaver_wait_release = true;", touch)
            self.assertIn("screensaver_wait_release = true;", sketch)
            self.assertIn('prefs.getUShort("bl_auto", 5U << 8)', sketch)
            self.assertIn("req.len != 3 && req.len != 4 && req.len != 6", sketch)
            self.assertIn("resp.payload[o++] = display_backlight.isOff() ? 0 : 1;", sketch)
            self.assertIn("ui_system_backlight_mode = nullptr;", sketch)
            handler = sketch.split("static void handle_display_settings(", 1)[1].split("static uint8_t status_led_sync_brightness", 1)[0]
            self.assertLess(handler.index("STATUS_BAD_VALUE"), handler.index("apply_backlight_settings("))
            self.assertIn("ui_defer_flags(UI_DEFER_DISPLAY_SETTINGS)", handler)

    def test_master_backlight_extension_is_scoped_and_preserves_legacy_length(self):
        source = (ROOT / "OpenFumeExtractorMaster/src/MasterScheduler.cpp").read_text(encoding="utf-8")
        setter = source.split("bool MasterScheduler::setDisplaySettings(", 1)[1].split("bool MasterScheduler::setUniversalProfile(", 1)[0]
        self.assertEqual(source.count("backlight_requested ? 6 : 4"), 1)
        self.assertIn("backlight_requested ? 6 : 4", setter)
        self.assertIn("backlight_requested && !rec->display_backlight_supported", setter)
        self.assertIn("backlight_requested && resp.len < 7", setter)
        self.assertIn("case MODULE_DISPLAY: led_pos = 22;", source)
        self.assertIn("rec->display_backlight_mode = resp.payload[24]", source)

if __name__ == "__main__":
    unittest.main()
