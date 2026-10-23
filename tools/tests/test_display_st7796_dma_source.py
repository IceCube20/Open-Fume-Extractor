from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
FOLDER = ROOT / "Module/DisplayModule_ST7796_320x480"
SOURCE = (FOLDER / "DisplayModule_ST7796_320x480.ino").read_text(encoding="utf-8-sig")
HEADER = (FOLDER / "src/OfeSt7796DmaBus.h").read_text()


class St7796DmaSource(unittest.TestCase):
    def test_only_st7796_owns_adapter(self):
        self.assertIn("#define DISPLAY_ST7796_ASYNC_DMA 0", SOURCE)
        self.assertIn("DISPLAY_ST7796_ASYNC_DMA && DISPLAY_DIRECT_PARTIAL_FLUSH && DISPLAY_USE_PANEL_HW_ROTATION", SOURCE)
        self.assertIn("new Arduino_ESP32SPI(", SOURCE)
        for name in ("DisplayModule_320x480", "DisplayModule_800x480", "DisplayModule_P4_800x480"):
            sketch = (ROOT / "Module" / name / (name + ".ino")).read_text(encoding="utf-8-sig")
            self.assertNotIn("OfeSt7796DmaBus", sketch)

    def test_single_owner_and_bounded_staging(self):
        self.assertIn("spi_bus_initialize(SPI2_HOST", HEADER)
        self.assertIn("device.queue_size = 2", HEADER)
        self.assertIn("maxTransferBytes = 32768U", HEADER)
        self.assertIn("SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL", HEADER)
        self.assertIn("internalReserve = 96U * 1024U", HEADER)
        self.assertIn("bytes > maxTileBytes", HEADER)
        self.assertNotIn("lv_display_flush_ready", HEADER)
        callback = HEADER.split("static void transferDone(", 1)[1].split("\n  void send(", 1)[0]
        self.assertNotIn("debug_", callback)
        self.assertNotIn("heap_caps_", callback)

    def test_no_early_ready_and_no_inflight_free(self):
        flush = SOURCE.split("static void lvgl_flush_cb(", 1)[1].split("static void lvgl_timer_handler_profiled", 1)[0]
        queued = flush.split("if (st7796_dma_bus.queuePixels", 1)[1].split("return;", 1)[0]
        self.assertNotIn("lv_display_flush_ready", queued)
        self.assertIn("st7796_pending_display = disp", queued)
        complete = SOURCE.split("static void st7796_complete_flush(", 1)[1].split("\n}\n", 1)[0]
        self.assertLess(complete.index("waitPixels()"), complete.index("lv_display_flush_ready"))
        replace = SOURCE.split("static bool st7796_replace_profile(", 1)[1].split("\n}\n", 1)[0]
        self.assertLess(replace.index("st7796_complete_flush"), replace.index("lv_display_set_buffers"))
        self.assertLess(replace.index("lv_display_set_buffers"), replace.index("heap_caps_free(previous)"))
        self.assertIn("heap_caps_free(previous2)", replace)
        self.assertIn("lv_display_set_flush_wait_cb(lvgl_disp, st7796_complete_flush)", SOURCE)
        self.assertEqual(SOURCE.count("lv_refr_now(lvgl_disp);\n#if OFE_ST7796_DMA_PATH\n  st7796_complete_flush(lvgl_disp);"), 2)


if __name__ == "__main__":
    unittest.main()
