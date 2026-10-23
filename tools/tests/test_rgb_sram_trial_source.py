"""Safety/provenance checks for the promoted S3 SRAM profile and labelled snapshot."""
from pathlib import Path
import runpy
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROD = ROOT / "Module/DisplayModule_800x480"
TRIAL = ROOT / "tools/benchmarks/DisplayRgbSramTrial"
PREPARE = runpy.run_path(str(ROOT / "tools/benchmarks/prepare_rgb_sram_trial.py"))


class RgbSramTrialSource(unittest.TestCase):
    def test_snapshot_matches_current_production(self):
        outputs = PREPARE["generated_files"]()
        for name, data in outputs.items():
            with self.subTest(file=name):
                self.assertEqual((TRIAL / name).read_bytes(), data)
        self.assertEqual((TRIAL / "DisplayRgbSramTrial.ino").read_bytes(),
                         (PROD / "DisplayModule_800x480.ino").read_bytes())
        self.assertEqual((TRIAL / "build_opt.h").read_bytes(),
                         (PROD / "build_opt.h").read_bytes() + b"\n-DOFE_RGB_SRAM_TRIAL=1\n")
        self.assertEqual((TRIAL / "SproutIcon.h").read_bytes(),
                         (PROD.parent / "DisplayModule_320x480/SproutIcon.h").read_bytes())
        self.assertFalse(any(name.endswith((".bin", ".elf")) or "build/" in name for name in outputs))

    def test_standard_uses_tested_psram_profile_and_unchanged_reserve(self):
        config = (PROD / "ofe_lv_conf.h").read_text()
        self.assertIn("#define OFE_RGB_WIDGET_POOL_PSRAM 0", config)
        self.assertIn('#include "src/OfeRgbWidgetPool.h"', config)
        self.assertIn("#define LV_MEM_POOL_ALLOC ofe_rgb_widget_pool_alloc", config)
        flags = (PROD / "build_opt.h").read_text()
        self.assertEqual(flags.count("-DOFE_RGB_WIDGET_POOL_PSRAM=1"), 1)
        self.assertNotIn("OFE_RGB_SRAM_TRIAL", flags)
        self.assertIn("#elif defined(OFE_RGB_SRAM_TRIAL) && OFE_RGB_SRAM_TRIAL",
                      (PROD / "DisplayModule_800x480.ino").read_text(encoding="utf-8-sig"))
        self.assertNotIn("OFE_RGB_WIDGET_POOL_PSRAM=1",
                         (ROOT / "Module/DisplayModule_P4_800x480/build_opt.h").read_text())
        self.assertIn("RUNTIME_RESERVE = 104U * 1024U", (PROD / "src/OfeDisplayMemory.h").read_text())

    def test_prepared_before_lvgl_and_no_internal_fallback(self):
        sketch = (PROD / "DisplayModule_800x480.ino").read_text(encoding="utf-8-sig")
        init = sketch.split("static void lvgl_init_ui() {", 1)[1].split("lv_tick_set_cb", 1)[0]
        self.assertLess(init.index("ofe_rgb_widget_pool_prepare()"), init.index("lv_init();"))
        self.assertIn("#if OFE_RGB_WIDGET_POOL_PSRAM", init)
        self.assertIn("return;", init.split("lv_init();", 1)[0])
        self.assertIn('#define OFE_MODULE_FW_SUFFIX "RC-SramTrial"', sketch)
        pool = (PROD / "src/OfeRgbWidgetPool.cpp").read_text()
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT", pool)
        self.assertNotIn("MALLOC_CAP_INTERNAL", pool)
        self.assertNotIn("heap_caps_free", pool)
        self.assertIn("bytes == LV_MEM_SIZE ? widget_pool : nullptr", pool)


if __name__ == "__main__":
    unittest.main()
