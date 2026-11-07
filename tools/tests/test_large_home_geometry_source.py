"""Keep the large-display extraction controls aligned in both ports."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class LargeHomeGeometryTests(unittest.TestCase):
    def test_large_ports_share_balanced_columns_and_button_gaps(self):
        for name in ("DisplayModule_800x480", "DisplayModule_P4_800x480"):
            with self.subTest(port=name):
                source = (ROOT / "Module" / name / f"{name}.ino").read_text(encoding="utf-8-sig")
                mode_x = int(re.search(r"lv_obj_set_pos\(ui_home_mode_dropdown, (\d+), 30\)", source)[1])
                mode_w = int(re.search(r"lv_obj_set_size\(ui_home_mode_dropdown, (\d+), 40\)", source)[1])
                power_x, power_w = map(int, re.search(
                    r"ui_home_power_input = lv_numeric_field\(ui_home_settings_card, (\d+), 30, (\d+),", source).groups())
                delay_x, delay_w = map(int, re.search(
                    r"ui_home_delay_input = lv_numeric_field\(ui_home_settings_card, (\d+), 30, (\d+),", source).groups())
                self.assertGreater(mode_w, 118)
                self.assertEqual(power_w, delay_w)
                self.assertEqual(power_x - mode_x - mode_w, 8)
                self.assertEqual(delay_x - power_x - power_w, 8)
                self.assertEqual(delay_x + delay_w, 376 - 12)
                after_x, after_w = map(int, re.search(
                    r"ui_home_afterrun_power_input = lv_numeric_field\(ui_home_settings_card, (\d+), 106, (\d+),", source).groups())
                self.assertEqual((after_x, after_w), (power_x, power_w))

                buttons = {}
                for key in ("continuous", "fan", "light", "io1", "io2"):
                    buttons[key] = tuple(map(int, re.search(
                        rf"ui_home_{key}_button = lv_small_button\(ui_home_output_card, (\d+), 32, (\d+),", source).groups()))
                x, width = buttons["continuous"]
                for first, second in (("fan", "light"), ("io1", "io2")):
                    first_x, first_w = buttons[first]
                    second_x, second_w = buttons[second]
                    self.assertEqual(first_x - x - width, 8)
                    self.assertEqual(second_x - first_x - first_w, 8)
                    self.assertEqual(second_x + second_w, 376 - 12)


if __name__ == "__main__":
    unittest.main()
