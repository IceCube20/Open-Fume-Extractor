"""Header and pairing-panel layout guards using the production font metrics."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NAMES = ("DisplayModule_320x480", "DisplayModule_800x480",
         "DisplayModule_ST7796_320x480")


def read(path):
    return path.read_text(encoding="utf-8-sig")


def header(source):
    return source.split("static void lv_add_header(", 1)[1].split(
        "static void lv_screen_switch(", 1)[0]


def label_box(source, name):
    line = re.search(r"lv_obj_t\* " + name + r"\s*=lv_label\(.*", source.replace(" = ", "="))
    if not line:
        raise AssertionError("Missing header label: " + name)
    line = line.group()
    x, y = map(int, re.search(r",\s*(\d+),\s*(\d+),", line).groups())
    width = int(re.search(r"UI_FONT_DEFAULT,\s*(\d+)\);", line).group(1))
    return x, y, width


class HeaderLayoutTests(unittest.TestCase):
    def test_header_bounds_text_and_all_indicators(self):
        for name in NAMES:
            with self.subTest(display=name):
                folder = ROOT / "Module" / name
                source = read(folder / (name + ".ino"))
                h = header(source)
                width = 800 if name == "DisplayModule_800x480" else 480
                font = read(folder / "openfume_montserrat_14_de.c")
                glyphs = font.split("glyph_dsc[] = {", 1)[1].split("};", 1)[0]
                advances = [int(v) for v in re.findall(r"\.adv_w\s*=\s*(\d+)", glyphs)]

                def text_width(text):
                    return sum((advances[ord(c)-31]+8)//16 for c in text)

                boxes = {key: label_box(h, key) for key in
                         ("sub", "title", "author", "clock_lbl", "alarm_lbl", "ble_lbl", "link_lbl")}
                eco = tuple(map(int, re.search(
                    r"eco_icon = lv_create_eco_sprout\(header, (\d+), (\d+)\)", h).groups()))
                boxes["eco_icon"] = (*eco, 20)
                order = ("sub", "title", "clock_lbl", "alarm_lbl", "eco_icon", "ble_lbl", "link_lbl")
                for key in order:
                    x, y, w = boxes[key]
                    self.assertGreaterEqual(x, 0)
                    self.assertGreaterEqual(y, 0)
                    self.assertLessEqual(x+w, width)
                    self.assertLessEqual(y+24, 42)
                for left, right in zip(order, order[1:]):
                    x, _, w = boxes[left]
                    self.assertLessEqual(x+w+2, boxes[right][0], (left, right))
                self.assertEqual(boxes["title"][::2], boxes["author"][::2])
                self.assertGreaterEqual(boxes["alarm_lbl"][2], 46)  # Bell, space, up to 255.
                self.assertIn('LV_SYMBOL_BELL " %u", active_alarm_count', source)
                self.assertIn("lv_show_alarms_event, LV_EVENT_CLICKED", h)
                self.assertLessEqual(text_width("Open Fume Extractor"), boxes["title"][2])
                self.assertLessEqual(text_width("by IceCube20"), boxes["author"][2])
                captions = re.findall(r'localized_subtitle = tr\("([^"]+)", "([^"]+)"\)', h)
                for pair in captions:
                    for caption in pair:
                        self.assertLessEqual(text_width(caption), boxes["sub"][2], caption)
                # Worst-width digits, not just the current date with narrow ones.
                self.assertLessEqual(text_width("88.88.8888"), boxes["clock_lbl"][2])
                self.assertLessEqual(text_width("23:59"), boxes["clock_lbl"][2])
                if width==480:
                    self.assertGreaterEqual(boxes["sub"][2], 114)
                    self.assertEqual(boxes["title"][0], 122)
                    self.assertEqual(boxes["clock_lbl"][0], 282)
                    self.assertGreaterEqual(boxes["ble_lbl"][2], 20)
                    self.assertGreaterEqual(boxes["link_lbl"][2], 24)

    def test_pairing_keyboard_and_dialog_preserve_header(self):
        copies = []
        for name in NAMES:
            ui = read(ROOT / "Module" / name / "src/OfeDisplayWifiUi.inc.h")
            copies.append(ui)
            self.assertIn("static constexpr int HEADER_HEIGHT=42;", ui)
            self.assertIn("return lv_display_get_vertical_resolution(nullptr)-HEADER_HEIGHT;", ui)
            self.assertIn("lv_obj_set_pos(root_,0,HEADER_HEIGHT);", ui)
            self.assertIn("h=contentHeight();", ui)
            self.assertIn("lv_obj_set_size(root_,w,h);", ui)
            self.assertIn("lv_obj_set_size(form_,w-24,h-54);", ui)
            self.assertIn("lv_obj_set_size(keyboard_,w,keyboardHeight());", ui)
            self.assertIn("lv_obj_set_height(ui->form_,contentHeight()-keyboardHeight()-54);", ui)
            self.assertIn("lv_obj_set_height(form_,contentHeight()-54);", ui)
            self.assertIn("lv_obj_set_size(dialog_,lv_display_get_horizontal_resolution(nullptr),contentHeight());", ui)
            for height in (320, 480):
                body = height-42
                keyboard = height//2
                form = body-keyboard-54
                self.assertGreaterEqual(form, 44)
                self.assertEqual(42+50+form+4, height-keyboard)
                self.assertGreaterEqual(body, 182)  # Confirmation panel.
            # Reuse the underlying live header, never register temporary header widgets.
            self.assertNotIn("lv_add_header", ui)
            self.assertIn("lv_obj_delete(root_); root_=nullptr;", ui)
        self.assertEqual(copies[0], copies[1])
        self.assertEqual(copies[0], copies[2])


if __name__ == "__main__":
    unittest.main()
