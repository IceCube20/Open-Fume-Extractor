from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
FOLDER = ROOT / "Module/DisplayModule_320x480"
SOURCE = (FOLDER / "DisplayModule_320x480.ino").read_text(encoding="utf-8-sig")
HEADER = (FOLDER / "src/OfeQspiFrameWorker.h").read_text()


class QspiAsyncSource(unittest.TestCase):
    def test_no_partial_or_timing_changes(self):
        for setting in ("DISPLAY_QSPI_FREQ 60000000", "DISPLAY_USE_PANEL_HW_ROTATION 0",
                        "DISPLAY_QSPI_ASYNC_FULL_FRAME 0", "DISPLAY_FAST_CANVAS_ROTATE1 1"):
            self.assertIn("#define " + setting, SOURCE)
        send = SOURCE.split("static bool qspi_transmit_full_frame(", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("bytes != QSPI_FRAME_BYTES", send)
        self.assertIn("AXS15231B_CASET, 0, DISPLAY_NATIVE_WIDTH - 1", send)
        self.assertIn("AXS15231B_RASET, 0, DISPLAY_NATIVE_HEIGHT - 1", send)
        self.assertIn("writeCommand(AXS15231B_RAMWR)", send)
        self.assertNotIn("lv_", send)

    def test_snapshot_is_not_a_render_buffer(self):
        self.assertIn("std::memcpy(snapshot_, frame, frameBytes)", HEADER)
        self.assertIn("bytes != frameBytes", HEADER)
        self.assertIn("MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_8BIT", HEADER)
        self.assertIn("internalReserve = 96U * 1024U", HEADER)
        self.assertNotIn("lv_", HEADER)
        self.assertNotIn("debug_", HEADER)
        submit = HEADER.split("bool submit(", 1)[1].split("bool collect(", 1)[0]
        self.assertLess(submit.index("memcpy"), submit.index("xTaskNotifyGive"))
        self.assertIn("if (!task_ || busy_", submit)

    def test_collect_and_forced_paths(self):
        flush = SOURCE.split("static void lvgl_flush_canvas_if_dirty(", 1)[1].split("\n}\n", 1)[0]
        self.assertLess(flush.index("qspi_collect_full_frame(force || paused)"), flush.index("!lvgl_canvas_dirty"))
        self.assertIn("if (qspi_async_active && !paused)", flush)
        self.assertIn("return OfeDisplayWifiUi::isOpen() || display_ble.active() || fw_update_active || status.update_active", SOURCE)
        self.assertIn("if (qspi_frame_worker.busy()) return", flush)
        self.assertIn("if (force) qspi_collect_full_frame(true)", flush)
        benchmark = SOURCE.split("static void qspi_manual_transport_benchmark()", 1)[1].split("\n}\n", 1)[0]
        self.assertLess(benchmark.index("qspi_collect_full_frame(true)"), benchmark.index("draw16bitBeRGBBitmap"))
        for name in ("display_benchmark_reset_counters(uint32_t now)", "display_benchmark_finish(uint32_t now)"):
            body = SOURCE.split("static void " + name, 1)[1].split("\n}\n", 1)[0]
            self.assertIn("qspi_collect_full_frame(true)", body)

    def test_panel_initialization_precedes_worker_and_gui(self):
        setup = SOURCE.split("void setup() {", 1)[1].split("void loop()", 1)[0]
        worker = setup.index("qspi_frame_worker.begin(qspi_transmit_full_frame)")
        for call in ("gfx->begin(DISPLAY_QSPI_FREQ)", "lvgl_force_refresh_once()", "display_wifi.begin("):
            self.assertLess(setup.index(call), worker)
        self.assertLess(worker, setup.index("display_gui.begin("))
        self.assertIn('!strcmp(command, "qspi sync")', SOURCE)
        self.assertIn('!strcmp(command, "qspi async")', SOURCE)

    def test_usb_reset_is_not_reported_as_unknown(self):
        self.assertIn('case ESP_RST_USB: return "USB reset";', SOURCE)


if __name__ == "__main__":
    unittest.main()
