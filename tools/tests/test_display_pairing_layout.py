"""Pairing-panel font/layout regressions, including real production font metrics."""
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DISPLAYS = ("DisplayModule_320x480", "DisplayModule_800x480", "DisplayModule_ST7796_320x480")


def read(path):
    return path.read_text(encoding="utf-8-sig")


def font_width(text, size):
    source = read(ROOT / "Module/DisplayModule_320x480" / f"openfume_montserrat_{size}_de.c")
    glyphs = source.split("glyph_dsc[] = {", 1)[1].split("};", 1)[0]
    advances = [int(n) for n in re.findall(r"\.adv_w\s*=\s*(\d+)", glyphs)]
    ranges = [tuple(map(int, n)) for n in re.findall(
        r"\.range_start = (\d+), .range_length = (\d+), .glyph_id_start = (\d+)", source)]
    width = 0
    for char in text:
        for start, count, first in ranges:
            if start <= ord(char) < start + count:
                width += (advances[first + ord(char) - start] + 8) // 16
                break
        else:
            raise AssertionError(f"Missing production glyph: {char!r}")
    return width


class PairingLayoutTests(unittest.TestCase):
    def test_all_three_displays_share_the_same_ui(self):
        copies = [read(ROOT / "Module" / name / "src/OfeDisplayWifiUi.inc.h") for name in DISPLAYS]
        self.assertEqual(copies[0], copies[1])
        self.assertEqual(copies[0], copies[2])
        ui = copies[0]
        self.assertIn("lv_obj_set_size(obj,width,44);", ui)
        self.assertIn("ofe_display_ui::dropdown(obj);", ui)
        self.assertIn("const bool wide=ofe_display_ui::wide(w);", ui)
        self.assertIn("setLabel(connection_,line);", ui)
        self.assertIn("setLabel(state_,line);", ui)
        self.assertIn("if (!dirty_ || reload_on_save_) fill();", ui)
        self.assertIn("if (display_ble.active()) { display_ble.cancel(); return; }", ui)
        self.assertNotIn("lv_add_header", ui)

    def test_labels_fit_with_production_fonts(self):
        folder = ROOT / "tools/tests/build-checks/display-pairing"
        if not (folder / "480-0.json").is_file():
            self.skipTest("Run display_hybrid_native/run_pairing_layout.ps1 first")
        for width in (480, 800):
            for lang in (0, 1):
                nodes = json.loads(read(folder / f"{width}-{lang}.json"))
                for node in nodes:
                    if node["kind"] == "input" or node["text"] in ("×", "↻"):
                        continue
                    text = node["text"]
                    size = 18 if text in ("Display connection", "Display-Verbindung", "WiFi", "WLAN", "Master") else 14
                    available = node["width"] - (24 if node["kind"] in ("select", "button") else 0)
                    self.assertLessEqual(font_width(text, size), available, (width, lang, text))


if __name__ == "__main__":
    unittest.main()
