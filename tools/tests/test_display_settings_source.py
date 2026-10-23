"""Ensure the independent Arduino sketches keep the same settings layout."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
NAMES = ("DisplayModule_320x480", "DisplayModule_800x480", "DisplayModule_ST7796_320x480")

class SettingsSourceTests(unittest.TestCase):
    def test_shared_layout_and_all_existing_functions_remain_wired(self):
        helpers = [(ROOT / "Module" / name / "src/OfeDisplaySettingsUi.inc.h").read_text(encoding="utf-8") for name in NAMES]
        self.assertTrue(all(helper == helpers[0] for helper in helpers))
        helper = helpers[0]
        for callback in ("lv_language_event", "lv_theme_event", "lv_brightness_up_event",
                         "lv_brightness_down_event", "lv_screensaver_timeout_event",
                         "lv_st7796_color_profile_event", "OfeDisplayWifiUi::openEvent",
                         "display_benchmark_open_event"):
            self.assertIn(callback, helper)
        self.assertIn("ofe_display_ui::wide(width)", helper)
        self.assertNotIn("lv_card(", helper)
        for name in NAMES:
            sketch = (ROOT / "Module" / name / (name + ".ino")).read_text(encoding="utf-8")
            self.assertIn('#include "src/OfeDisplaySettingsUi.inc.h"', sketch)
            width = 776 if "800x480" in name else 456
            self.assertIn(f"lv_create_display_settings(system_body, {width});", sketch)
            self.assertIn('ofe_display_ui::content(ui_system_screen)', sketch)
            self.assertIn("lv_set_visible(ui_system_benchmark_button, status.developer_mode);", sketch)

if __name__ == "__main__":
    unittest.main()
